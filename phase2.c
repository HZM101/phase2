/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona South
   Computer Science 452

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
void unbloxodus(int);
static void nullsys(sysargs *);
void insert_blocked_proc(int);

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>




/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

/* the mail boxes */
mail_box MailBoxTable[MAXMBOX];
mail_slot MailSlotTable[MAXSLOTS];
mail_proc MBoxProcs[MAXPROC];

/* Global Mail Box ID tracker */
int global_mbox_id;

/* Empty mailbox for setting the initialization. */
mail_box empty_mbox = {0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,};

/* Empty mail slot for setting the initialization. */
mail_slot empty_slot = {0, NULL, NULL, NULL, NULL, NULL,};

/* Empty proc for setting the initialization. */
mail_proc empty_proc = { NULL, NULL, NULL, NULL, NULL, NULL, NULL,};

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
   if (DEBUG2 && debugflag2)
      console("start1(): at beginning\n");

   check_kernel_mode("start1");

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
   for(int i = 0; i < MAXSLOTS)
   {
      MailSlotTable[i] = empty_slot;
   }

   /* Proc */
   for(int i = 0; i < MAXPROC)
   {
      MBoxProcs[i] = empty_proc;
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
   return MailBoxTable[i].mbox.id;

} /* MboxCreate */

int MboxRelease(int mailboxID)
{

} /* MboxRelease */

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

   int i, j = 0;

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
         MBoxProcs[getpid() % MAXPROC].pid = getpid();
         MBoxProcs[getpid() % MAXPROC].blocked = BLOCKED;
         MBoxProcs[getpid() % MAXPROC].blocked_how = MBOXZEROSENDING;
         MBoxProcs[getpid() % MAXPROC].message_size = msg_size;
         memcpy(&MBoxProcs[getpid() % MAXPROC].message, msg_ptr, msg_size);
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* if process is zapped or mailbox was released then return -3 */
         if( block_me(MBOXZEROSENDING) == -1 || MailBoxTable[i].status == RELEASED)
         {
            /* Testing for last process blocked if so unblock the releaser.*/
            if(MBoxProcs[getpid() % MAXPROC].last_status == LASTPROC)
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
         unbloxodus(i);
         return 0;
      }
      else /* Mailbox is a sender, block process on the mailbox. */
      {
         /* Initialize the mbox_proc values */
         MBoxProcs[getpid() % MAXPROC].pid = getpid();
         MBoxProcs[getpid() % MAXPROC].blocked = BLOCKED;
         MBoxProcs[getpid() % MAXPROC].blocked_how = MBOXZEROSENDING;
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* if process is zapped or mailbox was released then return -3 */
         if( block_me(MBOXZEROSENDING) == -1 || MailBoxTable[i].status == RELEASED)
         {
            /* Testing for last process blocked if so unblock the releaser.*/
            if(MBoxProcs[getpid() % MAXPROC].last_status == LASTPROC)
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
         MBoxProcs[getpid() % MAXPROC].pid = getpid();
         MBoxProcs[getpid() % MAXPROC].blocked = BLOCKED;
         MBoxProcs[getpid() % MAXPROC].blocked_how = MBOXFULL;
         MailBoxTable[i].num_blocked_procs++;
         insert_blocked_proc(i);

         /* if process is zapped or mailbox was released then return -3 */
         if( block_me(MBOXFULL) == -1 || MailBoxTable[i].status == RELEASED)
         {
            /* Testing for last process blocked if so unblock the releaser.*/
            if(MBoxProcs[getpid() % MAXPROC].last_status == LASTPROC)
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
            console("MboxSend(): Error. Overflow of MailSlotTable.\n")
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
   unbloxodus(i);

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
} /* MboxReceive */

int MboxCondSend(int mailboxID, void *message, int message_size)
{

} /* MboxCondSend */

int MboxCondReceive(int mailboxID, void *message, int max_message_size)
{

} /* MboxCondReceive */

int waitdevice(int type, int unit, int *status)
{

} /* waitdevice */


/* -------------------------------------------------------------------------
   Name - unbloxodus()
   Purpose - unblock one process blocked on a mailbox
   Parameters - int mailbox
   Returns - Nothing
   --------------------------------------------------------------------------*/
void unbloxodus(int mailbox)
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
} /* unbloxodus */


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



