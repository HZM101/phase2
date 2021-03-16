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