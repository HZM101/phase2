/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona South
   Computer Science 452

   @authors: Erik Ibarra Hurtado, Hassan Martinez, Victor Alvarez

   ------------------------------------------------------------------------ */

#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <string.h> 
#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include "message.h"

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
void disk_handler(int, void *);
void clock_handler2(int, void *);
void terminal_handler(int, void *);
void syscall_handler(int, void *);
void remove_mail_slot(int);
void unblock_mail_ptr(int);
static void nullsys(sysargs *);
void insert_blocked_proc(int);
void remove_blocked_proc(int);
static void enableInterrupts(void);
void disableInterrupts(void);  
void insert_mail_slot(int, int);  
void check_kernel_mode(void);      
   
/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

/* the mail boxes */
mail_box MailBoxTable[MAXMBOX];
mail_slot MailSlotTable[MAXSLOTS];                        
mbox_proc MboxProcs[MAXPROC];  

/* Empty mailbox to set initialization */                
mail_box empty_mbox = {0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* Empty mailslot to set initialization */                
mail_slot empty_slot = {0, NULL, NULL, NULL, NULL, NULL};

/* Empty proc to set initialization */                   
mbox_proc empty_proc = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* Global Mail Box ID tracker */
int global_mbox_id;

/* Variable for interrupt vector by USLOSS */
void(*int_vec[NUM_INTS])(int dev, void * unit);

/* Variable for system call vector by USLOSS */
void(*sys_vec[MAXSYSCALLS])(sysargs * args);

/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
   int kid_pid, status;

   if (DEBUG2 && debugflag2)
      console("start1(): at beginning\n");

   check_kernel_mode();

   /* Disable interrupts */
   disableInterrupts();

   /* Initialize the mail box table, slots, & other data structures.
    * Initialize int_vec and sys_vec, allocate mailboxes for interrupt
    * handlers.  Etc... */

   /* Setting mail box structure */
   global_mbox_id = 0;

   /* Mail box table */
   for(int i = 0; i < MAXMBOX; i++)
   {
      MailBoxTable[i] = empty_mbox;
   }

   /* Mail slot table */
   for(int i = 0; i < MAXSLOTS; i++)
   {
      MailSlotTable[i] = empty_slot;
   }

   /* Proc */
   for(int i = 0; i < MAXPROC; i++)
   {
      MboxProcs[i] = empty_proc;
   }

   /* I/O */
   for(int i = 0; i < 7; i++)
   {
      MboxCreate(0, MAX_MESSAGE);
   }

   /* Setting interrupt handler */
   int_vec[CLOCK_DEV] = clock_handler2;
   int_vec[DISK_DEV] = disk_handler;
   int_vec[TERM_DEV] = terminal_handler;

   /* Setting sys_vec array */
   int_vec[SYSCALL_INT] = syscall_handler;

   /* Setting system call handler for nullsys */
   for(int i = 0; i < MAXSYSCALLS; i++)
   {
      sys_vec[i] = nullsys;
   }

   enableInterrupts();

   /* Create a process for start2, then block on a join until start2 quits */
   if (DEBUG2 && debugflag2)
      console("start1(): fork'ing start2 process\n");
   kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
   if ( join(&status) != kid_pid ) {
      console("start2(): join returned something other than start2's pid\n");
   }

   return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
   check_kernel_mode();
   disableInterrupts();

   /* For interation under the MailBoxTable. */
   int i = 0;

   /* Cheking if their is no mailboxes are available to return -1. */
   while (MailBoxTable[i].is_free != 0)
   {
      i++;
      if(i >= MAXMBOX)
      {
         if(DEBUG2 && debugflag2)
         {
            console("MboxCreate(): no mailboxes available. Return -1\n");
         }
         return -1;
      }
   }
   
   /* Cheking if slot_size is incorrect if so return -1. */
   if(slot_size > MAX_MESSAGE)
   {
      if(DEBUG2 && debugflag2)
      {
         console("MboxCreate(): slot_size is incorrect. Return -1\n");
      }
      return -1;
   }

   /* Cheking for next available mailbox id. */
   if(global_mbox_id >= MAXMBOX || MailBoxTable[global_mbox_id].status == ACTIVE)
   {
      for(int j = 0; j < MAXMBOX; j++)
      {
         if(MailBoxTable[j].status == INACTIVE)
         {
            global_mbox_id = j;
            break;
         }
      }
   }

   /* Initialize the mailbox. */
   MailBoxTable[i].is_free = 1;
   MailBoxTable[i].status = ACTIVE;
   MailBoxTable[i].num_slots = slots;
   MailBoxTable[i].max_slot_size = slot_size;
   MailBoxTable[i].num_used_slots = 0;
   MailBoxTable[i].num_blocked_procs = 0;

   enableInterrupts();

   /* Return mailbox ID "global_mbox_id++" */
   MailBoxTable[i].mbox_id = global_mbox_id++;
   return MailBoxTable[i].mbox_id;

} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
   disableInterrupts();
   check_kernel_mode();

   int i = 0;
   int j = 0;

   /* If debug is on. */
   if(DEBUG2 && debugflag2)
   {
      console("MboxSend(): called with mbox_id: %d, msg_ptr: %d, msg_size: &d\n", 
         mbox_id, msg_ptr, msg_size);
   }

   /* Checking for an invalid mbox_id if so return -1. */
   if(mbox_id < 0 || mbox_id >= MAXMBOX)
   {
      if(DEBUG2 && debugflag2)
      {
         console("MboxSend(): called with invalid mbox_id: %d. Return -1\n", mbox_id);
      }
      enableInterrupts();
      return -1;
   }

   /* Checking for mbox_id on mailbox table if not found return -1. */
   while(MailBoxTable[i].mbox_id != mbox_id)
   {
      i++;
      if( i == MAXMBOX)
      {
         if(DEBUG2 && debugflag2)
         {
            console("MboxSend(): mailbox not found in the mailbox table. Return -1\n");
         }
         enableInterrupts();
         return -1;
      }
   }

   /* Checking for status of mailbox table if INACTIVE return -1. */
   if(MailBoxTable[i].status == INACTIVE)
   {
      if(DEBUG2 && debugflag2)
      {
         console("MboxSend(): mailbox ID is inactive. Return -1\n");
      }
      enableInterrupts();
      return -1;
   }

   /* Checking if msg_size is bigger than max_slot_size if so return -1. */
   if(msg_size > MailBoxTable[i].max_slot_size)
   {
      if(DEBUG2 && debugflag2)
      {
         console("MboxSend(): messege size is too large. Return -1\n");
      }
      enableInterrupts();
      return -1;
   }

   /* Checking for empty slot mailbox. */
   if(MailBoxTable[i].num_slots == 0)
   {
      /* Check for blocked receiver. */
      if(MailBoxTable[i].proc_ptr == NULL)
      {
         /* Initialize the mbox_proc values */
         MboxProcs[getpid() % MAXPROC].pid = getpid();
         MboxProcs[getpid() % MAXPROC].blocked = BLOCKED;
         MboxProcs[getpid() % MAXPROC].blocked_how = MBOXZEROSENDING;
         MboxProcs[getpid() % MAXPROC].message_size = msg_size;
         memcpy(&MboxProcs[getpid() % MAXPROC].message, msg_ptr, msg_size);
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* if process is zapped or mailbox was released then return -3 */
         if( block_me(MBOXZEROSENDING) == -1 || MailBoxTable[i].status == RELEASED)
         {
            /* Testing for last process blocked if so unblock the releaser.*/
            if(MboxProcs[getpid() % MAXPROC].last_status == LASTPROC)
            {
               /* Unblocking */
               unblock_proc(MailBoxTable[i].releaser_pid);
            }

            return -3;
         }

         return 0;

      }

      if(MailBoxTable[i].proc_ptr != NULL && MailBoxTable[i].proc_ptr->blocked_how == MBOXZERORECEIVING)
      {

         /* Checking the size of receiver buffer. */
         if(MailBoxTable[i].proc_ptr->message_size < msg_size)
         {
            if(DEBUG2 && debugflag2)
            {
               console("MboxSend(): message is too big for receiver buffer. Return -1\n");
            }
            return -1;
         }

         /* Set message to receiver under the message ptr. */
         if(msg_ptr != NULL)
         {
            MailBoxTable[i].proc_ptr->message_size = msg_size;
            memcpy(&MailBoxTable[i].proc_ptr->message, msg_ptr, msg_size);
         }
         unblock_mail_ptr(i);
         return 0;
      }
      else /* Mailbox is a sender, block process on the mailbox. */
      {
         /* Initialize the mbox_proc values */
         MboxProcs[getpid() % MAXPROC].pid = getpid();
         MboxProcs[getpid() % MAXPROC].blocked = BLOCKED;
         MboxProcs[getpid() % MAXPROC].blocked_how = MBOXZEROSENDING;
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* if process is zapped or mailbox was released then return -3 */
         if( block_me(MBOXZEROSENDING) == -1 || MailBoxTable[i].status == RELEASED)
         {
            /* Testing for last process blocked if so unblock the releaser.*/
            if(MboxProcs[getpid() % MAXPROC].last_status == LASTPROC)
            {
               /* Unblocking */
               unblock_proc(MailBoxTable[i].releaser_pid);
            }
            return -3;
         }
      }
      return 0;
   }

   /* Checking for available mail slots. */
   if(MailBoxTable[i].num_used_slots >= MailBoxTable[i].num_slots && MailBoxTable[i].num_slots > 0) 
   {
         /* Initialize the mbox_proc values */
         MboxProcs[getpid() % MAXPROC].pid = getpid();
         MboxProcs[getpid() % MAXPROC].blocked = BLOCKED;
         MboxProcs[getpid() % MAXPROC].blocked_how = MBOXFULL;
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* if process is zapped or mailbox was released then return -3 */
         if( block_me(MBOXFULL) == -1 || MailBoxTable[i].status == RELEASED)
         {
            /* Testing for last process blocked if so unblock the releaser.*/
            if(MboxProcs[getpid() % MAXPROC].last_status == LASTPROC)
            {
               /* Unblocking */
               unblock_proc(MailBoxTable[i].releaser_pid);
            }

            return -3;
         }
   }

   /* Allocating slot from Mailslot table. */
   while(MailBoxTable[j].is_free != 0)
   {
      j++;
      /* More than allow slots causes an overflow then halt(1). */
      if(j >= MAXSLOTS)
      {
         if(DEBUG2 && debugflag2)
         {
            console("MboxSend(): Error. Overflow of MailSlotTable.\n");
         }
         halt(1);
      }
   }

   /* Setting mail_slot struct. */
   MailSlotTable[j].is_free = 1;
   MailSlotTable[j].mbox_id = MailBoxTable[i].mbox_id;
   memcpy(&MailSlotTable[j].message, msg_ptr, msg_size);
   MailSlotTable[j].message_size = msg_size;
   insert_mail_slot(i, j);
   MailBoxTable[i].num_used_slots++;
   unblock_mail_ptr(i);

   enableInterrupts();
   return 0;

} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
   disableInterrupts();
   check_kernel_mode();

   int i = 0;
   int message_size;

   /* Checking mailboxtable for mbox_id*/
   while (MailBoxTable[i].mbox_id != mbox_id)
   {
      i++;
      /* Invalid args mbox_id not found*/
      if (i == MAXMBOX)
      {
         return -1;
      }
   }

   /* Check for mailbox messages at a nonzero-slot mailboxes. */
   if (MailBoxTable[i].num_used_slots == 0 && MailBoxTable[i].num_slots > 0)
   {

      /* Initialize the mbox_proc values */
      MboxProcs[getpid()%MAXPROC].pid = getpid();
      MboxProcs[getpid()%MAXPROC].blocked = BLOCKED;
      MboxProcs[getpid()%MAXPROC].blocked_how = MBOXEMPTY;
      MailBoxTable[i].num_blocked_procs++;


      insert_blocked_proc(i);
      /* if process was zapped while blocked or status is RELEASED then return -3. */
      if (block_me(MBOXEMPTY) == -1 || (MailBoxTable[i].status == RELEASED))
      {
         return -3;
      }
   }

   /* Check a zero-slot mailbox table */
   if (MailBoxTable[i].num_slots == 0)
   {
      if (MailBoxTable[i].proc_ptr == NULL)
      {

         /* Initialize the mbox_proc values */
         MboxProcs[getpid()%MAXPROC].pid = getpid();
         MboxProcs[getpid()%MAXPROC].blocked = BLOCKED;
         MboxProcs[getpid()%MAXPROC].blocked_how = MBOXZERORECEIVING;
         MboxProcs[getpid()%MAXPROC].message_size = msg_size;
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* If process was zapped at blocked state or status is RELEASED then return -3 */ 
         if (block_me(MBOXZERORECEIVING) == -1 || MailBoxTable[i].status == RELEASED)
         {
            return -3;
         }

         /* Copy message to msg_ptr then return it. */
         memcpy(msg_ptr, &MboxProcs[getpid()%MAXPROC].message, MboxProcs[getpid()%MAXPROC].message_size);
         return MboxProcs[getpid()%MAXPROC].message_size;
      }
      
      /* Not NULL process to sender blocked to check size. */
      if (MailBoxTable[i].proc_ptr != NULL && MailBoxTable[i].proc_ptr->blocked_how == MBOXZEROSENDING)
      {
         /* Test size of the message if it is too big then return -1. */
         if (MailBoxTable[i].proc_ptr->message_size > msg_size)
         {
            return -1;
         }

         /* Copy message to sender then return it. */
         message_size = MailBoxTable[i].proc_ptr->message_size;
         memcpy(msg_ptr, &MailBoxTable[i].proc_ptr->message, message_size);
         unblock_mail_ptr(i);
         return message_size;
      }
      else
      {
         /* Initialize the mbox_proc values */
         MboxProcs[getpid()%MAXPROC].pid = getpid();
         MboxProcs[getpid()%MAXPROC].blocked = BLOCKED;
         MboxProcs[getpid()%MAXPROC].blocked_how = MBOXZERORECEIVING;
         MboxProcs[getpid()%MAXPROC].message_size = msg_size;
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* if zapped while blocked or status is RELEASED then return -3. */ 
         if (block_me(MBOXZERORECEIVING) == -1 || MailBoxTable[i].status == RELEASED)
         {
            return -3;
         }
      }

      /* Copy message to msg_ptr then return it. */
      memcpy(msg_ptr, &MboxProcs[getpid()%MAXPROC].message, MboxProcs[getpid()%MAXPROC].message_size);
      return MboxProcs[getpid()%MAXPROC].message_size;
   }

   /* Testing for size of message if to big then return -1. */
   if (MailBoxTable[i].slot_ptr->message_size > msg_size)
   {
      return -1;
   }

   message_size = MailBoxTable[i].slot_ptr->message_size;
   memcpy(msg_ptr, &MailBoxTable[i].slot_ptr->message, message_size);
   remove_mail_slot(i);
   unblock_mail_ptr(i);
   
   return message_size;

} /* MboxReceive */


/* ------------------------------------------------------------------------
   Name - MboxCondSend
   Purpose - Put a message into a slot for the indicated mailbox.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - -3 if process is zap'd
             -2 if mailbox is full, message not sent, or no mailbox slots
                available
             -1 if illegal values given as arguments
              0 if message sent successfully
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxCondSend(int mbox_id, void *message, int msg_size)
{
   disableInterrupts();
   check_kernel_mode();
   int i, j = 0;
   
   /* Checking validity of mbox_id. */
   if(mbox_id < 0 || mbox_id >= MAXMBOX)
   {
      if(DEBUG2 && debugflag2)
      {
         console("MboxCondSend(): called with invalid mbox_id: %d. Return -1\n");
      }
      enableInterrupts();
      return -1;
   }

   /* Cheking if their is no mailboxes are available to return -1. */
   while (MailBoxTable[i].mbox_id != mbox_id)
   {
      i++;
      if (i == MAXMBOX)
      {
         if (DEBUG2 && debugflag2)
         {
            console ("MboxCondSend(): mailbox not found in the mailbox table. Return -1\n");
         }
         return -1;
      }
   }

   /* Testing for illegal values if so return -1. */
   if (msg_size > MailBoxTable[i].max_slot_size || MailBoxTable[i].status == RELEASED)
   {
      return -1;
   }

   /* Checking any unused slots if so return -2.*/
   if (MailBoxTable[i].num_used_slots >= MailBoxTable[i].num_slots && MailBoxTable[i].num_slots > 0)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("MboxCondSend(): There are no unused mailbox slots. Return -2\n");
      }
      return -2;
   }

   /* Check a zero-slot mailbox */
   if (MailBoxTable[i].num_slots == 0)
   {
      /* Check NULL process under mailbox if so return -2. */
      if (MailBoxTable[i].proc_ptr == NULL)
      {
         return -2;
      }
      
      /* Check a Not NULL process under mailbox. */
      if (MailBoxTable[i].proc_ptr != NULL)
      {
         
         /* Check status & message. */
         if (MailBoxTable[i].status == RELEASED || MailBoxTable[i].proc_ptr->message_size < msg_size)
         {
            return -1;
         }
         memcpy(&MailBoxTable[i].proc_ptr->message, message, msg_size);
         unblock_mail_ptr(i);
         return 0;
      }
      return 0;
   }

   /* Cheking availability under next free slot. */
   while (MailSlotTable[j].is_free != 0)
   {
      j++;
      /* Check if their is no slots available then return -2. */
      if (j >= MAXSLOTS)
      {
         return -2;
      }
   }

   if(MailBoxTable[i].status == RELEASED)
   {
      return -1;
   }

   /* Setting mail_slot struct. */
   MailSlotTable[j].is_free = 1;
   MailSlotTable[j].mbox_id = MailBoxTable[i].mbox_id;
   memcpy(&MailSlotTable[j].message, message, msg_size);
   MailSlotTable[j].message_size = msg_size;
   insert_mail_slot(i, j);
   MailBoxTable[i].num_used_slots++;
   unblock_mail_ptr(i);

   /* Test for zap. */
   if(is_zapped() == 1)
   {
      return -3;
   }

   enableInterrupts();
   return 0;
} /* MboxCondSend */


/* ------------------------------------------------------------------------
   Name - MboxCondReceive
   Purpose - Put a message into a slot for the indicated mailbox.
   Parameters - mailbox id, pointer to data of msg, # of bytes in buffer.
   Returns - -3 if process is zap'd
             -2 if mailbox empty or no message to receive
             -1 if illegal values
              0 if successful
----------------------------------------------------------------------- */
int MboxCondReceive(int mailboxID, void *message, int max_message_size)
{
   disableInterrupts();
   check_kernel_mode();

   int i = 0;
   int message_size;

   /* Checking for the mailbox ID in the table. Also testing for illegal values. */
   while(MailBoxTable[i].mbox_id != mailboxID)
   {
      i++;
      if    (i == MAXMBOX 
            || MailBoxTable[i].status == RELEASED 
            || MailBoxTable[i].slot_ptr->message_size > max_message_size)
      {
         return -1;
      }
   }

   /* Checking for any slots in the table. */
   if(MailBoxTable[i].num_used_slots == 0)
   {
      return -2;
   }

   /* Saving message size of mailbox table. */
   message_size = MailBoxTable[i].slot_ptr->message_size;

   /* Check status again. */
   if(MailBoxTable[i].status == RELEASED)
   {
      if(DEBUG2 && debugflag2)
      {
         console("MboxCondReceive(): mailbox was released. Return -1\n");
      }
      return -1;
   }

   /* Save message to receiver message buffer. */
   memcpy(message, &MailBoxTable[i].slot_ptr->message, message_size);

   /* Release mailbox slot & unblock sender. */
   remove_mail_slot(i);
   unblock_mail_ptr(i);

   /* Test zap status if zap then return -3. */
   if(is_zapped() == 1)
   {
      if(DEBUG2 && debugflag2)
      {
         console("MboxCondReceive(): message is zap. Return -3\n");
      }
      return -3;
   }

   return 0;
} /* MboxCondReceive */


/* ------------------------------------------------------------------------
   Name - MboxRelease
   Purpose - releases a previously created mailbox
   Parameters - mailbox id.
   Returns - -3 if process was zapped while releasing the mailbox
             -1 the mailbox ID is not a mailbox that is in use
              0 successful completion
   ----------------------------------------------------------------------- */
int MboxRelease(int mailboxID)
{
     int i = 0;

   //mark mailbox as being released
   //return -1 mailbox id is not a mailbox in use
   while ( MailBoxTable[i].mbox_id != mailboxID)
   {
      i++;
      if ( i >= MAXMBOX)
      {
         return -1;
      }
   }
   MailBoxTable[i].status = RELEASED;
   MailBoxTable[i].releaser_pid = getpid();
   MailBoxTable[i].is_free = 0;
   MailBoxTable[i].mbox_id = NULL;

   //reclaim the mailslots from the mailslot table
   if (MailBoxTable[i].num_used_slots > 0)
   {
      while (MailBoxTable[i].num_used_slots > 0)
      {
         //release mailslots
         remove_mail_slot(i);
      }
   }

   //check for blocked procs and unblock them
   //block the releaser so that unblocked procs have a chance to finish
   if (MailBoxTable[i].num_blocked_procs > 0)
   {       
      block_me(MBOXRELEASING);
   }

   //return 0 if successful
   return 0;
} /* MboxRelease */


/* -------------------------------------------------------------------------
   Name - unblock_mail_ptr()
   Purpose - unblock one process blocked on a mailbox
   Parameters - int mailbox
   Returns - Nothing
   --------------------------------------------------------------------------*/
void unblock_mail_ptr(int mailbox)
{
   int pid;
     
   if(MailBoxTable[mailbox].proc_ptr != NULL) 
   {                                            
      pid = MailBoxTable[mailbox].proc_ptr->pid;
      MailBoxTable[mailbox].num_blocked_procs--;
      remove_blocked_proc(mailbox);
      unblock_proc(pid);
   }
   return;
} /* unblock_mail_ptr */


/* -------------------------------------------------------------------------
   Name - insert_blocked_proc()
   Purpose - insert proc to the linked list of blocked processes on a mailbox
   Parameters - int mailbox table slot
   Returns - Nothing
   --------------------------------------------------------------------------*/
void insert_blocked_proc(int mailbox)
{
  
   mbox_proc_ptr walker;
   mbox_proc_ptr previous;
   int i = getpid()%MAXPROC;
   walker = MailBoxTable[mailbox].proc_ptr;
   if (walker == NULL)
   {
     
      MailBoxTable[mailbox].proc_ptr = &MboxProcs[i];
    
   }
   else
   {
      while (walker != NULL)
      {
         previous = walker;
         walker = walker->next_proc_ptr;
      }
      previous->next_proc_ptr = &MboxProcs[i];
      
   }
  
   return;
} /* insert_blocked_proc */


/* -------------------------------------------------------------------------
   Name - remove_mail_slot()
   Purpose - maintain the linked list of mailslots in the mbox and mailslot
             structs, release mailbox slots from mailslottable
   Parameters - int mbox_index
   Returns - Nothing
   --------------------------------------------------------------------------*/
void remove_mail_slot(int mbox_index)
{
   int j = 0;
   while (&MailSlotTable[j] != MailBoxTable[mbox_index].slot_ptr)
   {
      j++;
   }
   if (MailBoxTable[mbox_index].slot_ptr->next_slot_ptr == NULL)
   {
      MailBoxTable[mbox_index].slot_ptr = NULL;
   }
   else
   {
      MailBoxTable[mbox_index].slot_ptr = MailBoxTable[mbox_index].slot_ptr->next_slot_ptr;
   }
   MailSlotTable[j] = empty_slot;

   //decrement used_slots on mailbox
   MailBoxTable[mbox_index].num_used_slots--;

   return;
} /* remove_mail_slot */


/* -------------------------------------------------------------------------
   Name - insert_mail_slot()
   Purpose - maintain the linked list of mailslots in the mbox and mailslot
             structs
   Parameters - int mailbox, int mailslot
   Returns - Nothing
   --------------------------------------------------------------------------*/
void insert_mail_slot(int mailbox, int mailslot)
{
   slot_ptr walker;
   slot_ptr previous;
   walker = MailBoxTable[mailbox].slot_ptr;
   if (walker == NULL)
   {
      MailBoxTable[mailbox].slot_ptr = &MailSlotTable[mailslot];
   }
   else
   {
      while (walker != NULL)
      {
         previous = walker;
         walker = walker->next_slot_ptr;
      }
      previous->next_slot_ptr = &MailSlotTable[mailslot];
   }
   return;
} /* insert_mail_slot */


/* -------------------------------------------------------------------------
   Name - disk_handler()
   Purpose - Code to handle Disk innterupts.
   Parameters - int device type, pointer to which unit  
   --------------------------------------------------------------------------*/
void disk_handler(int dev, void *punit)
{
   if (dev != DISK_DEV)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("disk_handler(): wrong device. return.\n");
      }
      return;   
   }

   int status;
   int result;
   int unit = (int)punit;
   int table_offset = 1;

   if (unit < 0 && unit > 1)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("disk_handler(): unit is out of range. return.\n");
      }
      return;   
   }
   
   /*check the mailbox set-up for the disk unit. If not OK, return */
   device_input(DISK_DEV, unit, &status);
   result = MboxCondSend((unit+table_offset), &status, sizeof(status));

   //more checking on the return result
   return;
} /* disk_handler */


/* -------------------------------------------------------------------------
   Name - syscall_handler()
   --------------------------------------------------------------------------*/
void syscall_handler(int dev, void *unit)
{
   sysargs *sys_ptr;

   sys_ptr = (sysargs *) unit;
   // Sanity check: if the interrupt is not SYSCALL_INT, halt(1)
   if (dev != SYSCALL_INT)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("syscall_handler(): Interrupt is not SYSCALL_INT. halt(1).\n");
      }
      halt(1);
   }
   /* check what system: if the call is not in the range between 0 and MAXSYSCALLS, , halt(1) */ 
   if (sys_ptr->number < 1 || sys_ptr->number >= MAXSYSCALLS)
   {
      console ("syscall_handler(): sys_ptr->number not in range.  halt(1).\n");
      halt(1);
   }
   
	/* Now it is time to call the appropriate system call handler */ 
	sys_vec[sys_ptr->number](sys_ptr);

   return;
} /* syscall_handler */


/* -------------------------------------------------------------------------
   Name - terminal_handler()
   Purpose - Code to handle terminal interupts.
   Parameters - int device type, pointer to which unit 
   --------------------------------------------------------------------------*/
void terminal_handler(int dev, void *punit)
{
   if (dev != TERM_DEV)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("terminal_handler(): wrong device. return.\n");
      }
      return;   
   }

   int status;
   int result;
   int unit = (int)punit;
   int table_offset = 3;

   if (unit < 0 && unit > 3)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("terminal_handler(): unit is out of range. return.\n");
      }
      return;   
   }
   
   /*check the mailbox set-up for the disk unit. If not OK, return */
   device_input(TERM_DEV, unit, &status);
   result = MboxCondSend((unit+table_offset), &status, sizeof(status));

   //more checking on the return result
   return;
} /* terminal_handler*/


/* -------------------------------------------------------------------------
   Name - clock_handler2()
   Purpose - Code to handle clock innterupts.
   Parameters - int device type, pointer to which unit 
   Returns - 
   --------------------------------------------------------------------------*/
void clock_handler2(int dev, void *punit)
{
   if (dev != CLOCK_DEV)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("clock_handler2(): wrong device. return.\n");
      }
      return;   
   }

   if (DEBUG2 && debugflag2)
   {
      console ("clock_handler2(): confirmed entry. target acquired.\n");
   }

   //clock_ticker is static so it keeps accumulating every call of clock_handler2 while program is being ran.
   static int clock_ticker = 0;   
   int dummy_value = 0; //for dummy message to send to anybody receiving on the clock mailbox

   //add a 20ms tick to clock_ticker
   clock_ticker = clock_ticker + CLOCK_MS;

   //check if 5 interrupts have happened
   if (clock_ticker % 100 == 0)
   {
      if (DEBUG2 && debugflag2)
      {
         console ("clock_handler2(): sending message.\n");
      }

      //Five interrupts have occured for a total of 100ms do a MboxCondSend to clock mailbox
      if (DEBUG2 && debugflag2)
      {
         console("clock_handler(): dummy_value %p\n", &dummy_value);
      }
      MboxCondSend(CLOCK_DEV, &dummy_value, sizeof(int)); //REVIEW THIS
      

      //Call timeslice to kick out current process if need be.
      time_slice();
   }

   return;
} /* clock_handler2*/


/* -------------------------------------------------------------------------
   Name - nullsys()
   Purpose - prints an error message
   Parameters - sysargs
   --------------------------------------------------------------------------*/
static void nullsys(sysargs *args)
{ 
	printf("nullsys(): Invalid syscall %d. Halting...\n", args->number); 
	halt(1);
} /* nullsys */


/* -------------------------------------------------------------------------
   Name - remove_blocked_proc()
   Purpose - remove proc from the linked list of blocked processes on a mailbox
             updates the data fields of the mbox proc
   Parameters - int mailbox
   Returns - Nothing
   --------------------------------------------------------------------------*/
void remove_blocked_proc(int mailbox)
{
   if (MailBoxTable[mailbox].proc_ptr->next_proc_ptr == NULL)
   {
      MailBoxTable[mailbox].proc_ptr = NULL;
   }
   else
   {
      MailBoxTable[mailbox].proc_ptr = MailBoxTable[mailbox].proc_ptr->next_proc_ptr;
      MboxProcs[getpid()%MAXPROC].next_proc_ptr = NULL;
   }

   MboxProcs[getpid()%MAXPROC].blocked = UNBLOCKED;
   MboxProcs[getpid()%MAXPROC].blocked_how = UNBLOCKED;
   

   return;
} /* remove_blocked_proc */


/* --------------------------------------------------------------------------------
   Name - enableInterrupts()
   Purpose - Enables the interrupts.
   Parameters - None
   Returns - Nothing
   ---------------------------------------------------------------------------------*/
static void enableInterrupts()
{
   psr_set((psr_get() | PSR_CURRENT_INT));
}  /*enableInterrupts*/


/* -------------------------------------------------------------------------
   Name - disableInterrupts()
   Purpose - Disables the interrupts.
   Parameters - None
   Returns - Nothing
   --------------------------------------------------------------------------*/
void disableInterrupts()        
{
  /* turn the interrupts OFF iff we are in kernel mode */
  if((PSR_CURRENT_MODE & psr_get()) == 0) {
    //not in kernel mode
    console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
    halt(1);
  } else
    /* We ARE in kernel mode */
    psr_set( psr_get() & ~PSR_CURRENT_INT );
} /* disableInterrupts */


/* -------------------------------------------------------------------------
   Name - check_kernel_mode()
   Purpose - Checks the PSR to see what the current mode is. Halts(1) if
             the current mode is user mode. Returns otherwise.
   Parameters - None
   Returns - Nothing
   --------------------------------------------------------------------------*/
void check_kernel_mode()
{
   // test if in kernel mode; halt if in user mode
   if ((PSR_CURRENT_MODE & psr_get()) == 0)
   {
      console("check_kernel_mode(): Error - current mode is user mode. Halt(1)\n");
      halt(1);
   }

   // return if in kernel mode
   return;
} /* check_kernel_mode */


/* -------------------------------------------------------------------------
   Name - check_io()
   Purpose - Checks for input/output.
   Parameters - None
   Returns - An integer.  Returns 0 for now, as a dummy function.
   --------------------------------------------------------------------------*/
int check_io()
{
   mbox_proc_ptr walker;
   int flag = 0;
   int i = 0;

   while (i < 7)
   {
      walker = MailBoxTable[i].proc_ptr;
      if (walker != NULL)
      {
         flag++;
      }
      i++;
   }

   if (flag > 0)
   {
      return 1;
   }
   else return 0;
} /* check_io */
