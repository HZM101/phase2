#define DEBUG2 1
#define RELEASED 2
#define ACTIVE 1
#define INACTIVE 0 
#define MBOXFULL 11
#define MBOXEMPTY 12
#define MBOXZEROSENDING 13
#define MBOXZERORECEIVING 14
#define MBOXRELEASING 15
#define BLOCKED 1
#define UNBLOCKED 0
#define LASTPROC 1

typedef struct mail_slot *slot_ptr;
typedef struct mailbox mail_box;
typedef struct mbox_proc *mbox_proc_ptr;
typedef struct mail_slot mail_slot;
typedef struct mbox_proc mbox_proc;

struct mailbox {
   int            is_free;
   int            mbox_id;
   int            status;
   int            num_slots;
   int            max_slot_size;
   mbox_proc_ptr  proc_ptr;
   slot_ptr       slot_ptr;
   int            num_used_slots;
   int            num_blocked_procs;
   int            releaser_pid;
   /* other items as needed... */
};

struct mail_slot {
   int            is_free;
   int            mbox_id;
   int            status;
   int            message_size;
   slot_ptr       next_slot_ptr;
   char           message[MAX_MESSAGE];
   /* other items as needed... */
};

struct psr_bits {
    unsigned int cur_mode:1;
    unsigned int cur_int_enable:1;
    unsigned int prev_mode:1;
    unsigned int prev_int_enable:1;
    unsigned int unused:28;
};

union psr_values {
   struct psr_bits bits;
   unsigned int integer_part;
};

struct mail_proc
{
   int               pid;
   int               blocked_how;
   int               blocked;
   mbox_proc_ptr     next_proc_ptr;
   int               message_size;
   char              message[MAX_MESSAGE];
   int               last_status;
}
