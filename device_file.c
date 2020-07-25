#include "device_file.h"
#include <linux/init.h>
#include <linux/fs.h> 	     /* file stuff */
#include <linux/kernel.h>    /* printk() */
#include <linux/module.h>  /* THIS_MODULE */
#include <asm/uaccess.h>  /* copy_to_user() */
#include <linux/uaccess.h>
#include <linux/slab.h> 
#include <linux/mutex.h>
#include <linux/kdev_t.h>
#include <linux/dcache.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/time.h>
#include <linux/cred.h>
#include <linux/string.h>
#include <linux/kallsyms.h>
#include <asm/segment.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/buffer_head.h>
#include <linux/ktime.h>	

// definitions

#define LINUX
#define LOG "/tmp/os.log"
#define GFP_KERNEL      (__GFP_RECLAIM | __GFP_IO | __GFP_FS)
#define LOG_LEN 1024
#define MAX_PATH_LEN 128
#define MAX_UID_LEN 32
#define wnum write_num_to_string_arr

// type definitions

typedef struct Node
{
    int uid;
    int secl;
    struct Node *next;
} user_entry;
typedef struct Nodef
{
    char *path;
    int secl;
    struct Nodef *next;
} file_entry;
typedef asmlinkage long (*custom_open) (const char __user *filename, int flags, umode_t mode);
typedef asmlinkage long (*sys_call_ptr_t)(const struct pt_regs *);

// variable definitions

static sys_call_ptr_t *sys_call_table;
custom_open open_table;
static user_entry *users = NULL;
static file_entry *files = NULL;
static int users_count = 0;
static int files_count = 0;
static int open_count = 0;

// function definitions

user_entry * find_user_entry( int uid ){
    user_entry * cur = users;
    while(cur != NULL) {
        if(cur->uid == uid)
            return cur;
        cur = cur->next;
    }
    return cur;
}

file_entry * find_file_entry( char * path ){
    file_entry * cur = files;
    while(cur != NULL) {
        if(strcmp(path, cur->path) == 0)
            return cur;
        cur = cur->next;
    }
    return cur;
}

void new_user(int uid, int secl)
{
    user_entry *entry = find_user_entry(uid);
    if(entry != NULL){
        entry->secl = secl;
        return;
    }
    entry = (user_entry*) kmalloc(sizeof(user_entry), GFP_KERNEL);
    if (entry == NULL)
    {
        printk(KERN_INFO "could not allocate new user entry due to error");
        return;
    }
    entry->uid = uid;
    entry->secl = secl;
    entry->next = users;
    users = entry;
    printk(KERN_INFO "new user entry added %d - uid(%d) - secl(%d)\n", ++users_count, uid, secl);
}

void new_file(char *path, int secl)
{   
    file_entry *entry = find_file_entry(path);
    if(entry != NULL){
        entry->secl = secl;
        return;
    }
    entry = (file_entry*) kmalloc(sizeof(file_entry), GFP_KERNEL);
    if (entry == NULL)
    {
        printk(KERN_INFO "could not allocate new file entry due to error");
        return;
    }
    entry->path = path;
    entry->secl = secl;
    entry->next = files;
    files = entry;
    printk(KERN_INFO "new file entry added %d - path(%s) - secl(%d)\n", ++files_count, path, secl);
}

struct file *open_file(const char *path, int flags, int rights) 
{
    struct file *file_ptr = NULL;
    mm_segment_t oldfs;

    oldfs = get_fs();
    set_fs(get_ds());
    file_ptr = filp_open(path, flags, rights);
    set_fs(oldfs);
    if (IS_ERR(file_ptr)) {
        return NULL;
    }
    return file_ptr;
}

void tracker(const char * filename, int secf,  int current_user_id, int secu, int wo, int rw){
    char data[LOG_LEN];
    struct timespec now;
    getnstimeofday(&now);
    
    sprintf(data, "record:\nuid: %d - secu: %d - filepath: %s - secf: %d - r(%d)w(%d)rw(%d) - time(%.2lu:%.2lu:%.2lu:%.6lu)\n", 
         current_user_id, secu, filename, secf, (!(wo|rw) ? 1: 0), (wo ? 1: 0), (rw ? 1: 0), (now.tv_sec / 3600) % (24),
                   (now.tv_sec / 60) % (60), now.tv_sec % 60, now.tv_nsec / 1000);

    struct file * log_file =  open_file(LOG, O_WRONLY|O_CREAT|O_APPEND, 0777);
    if(log_file == NULL){
        sprintf(data, "could not open logger file");
        return;
    }
    mm_segment_t oldfs;
    unsigned long long offset = 0;
    oldfs = get_fs();
    set_fs(get_ds());
    vfs_write(log_file, data, strlen(data), &offset);
    set_fs(oldfs);
    filp_close(log_file, NULL);
}

static asmlinkage long open_syscall(const char __user *filename, int flags, umode_t mode)
{
    user_entry * iuser;
    file_entry * ifile;
    int current_user_id = (int) get_current_user()->uid.val;
    int secu = 0, secf = 0;
	
    char *ptr = kfilename, *buffer = filename;
    char kfilename[MAX_PATH_LEN];
    int bytes_count_cpy = MAX_PATH_LEN;
    while (bytes_count_cpy--){
        get_user(*ptr, buffer++);
        if(*(ptr++) == '\0')
            break;
    }

    int write_only = flags & O_WRONLY;
	int read_write = flags & O_RDWR;
	
    if((iuser = find_user_entry(current_user_id))!= NULL)
        secu = iuser->secl;

    if((ifile = find_file_entry(kfilename))!= NULL){
        secf = ifile->secl;
        tracker(kfilename, secf, current_user_id,  secu, write_only, read_write );
    }

    if (secu == secf)
		return open_table(filename, flags, mode);
    
    if( secu < secf) {
        if(write_only)
            return open_table(filename, flags, mode);
        printk(KERN_INFO "can not read\n");
        return -1;
    } else {
        if(!(write_only|read_write))
            return open_table(filename, flags, mode);
        printk(KERN_INFO "can not write\n");
        return -1;
    }
    
    return open_table(filename, flags, mode);
}


static ssize_t device_file_read(struct file *file_ptr, char __user *user_buffer, size_t count, loff_t *position) {
	printk( KERN_NOTICE "device file is read at offset = %i, read byters count = %u", (int)*position, (unsigned int) count);	
	
    char *read_data = (char*) kmalloc(((MAX_PATH_LEN * files_count + MAX_UID_LEN * users_count) + 1) * sizeof(char), GFP_KERNEL);
    char *read_data_ptr;

    file_entry *ifile = files;
    user_entry *iuser = users;

    sprintf(read_data, "");
    while (ifile != NULL)
    {
        sprintf(read_data, "%s%d%s:", read_data, ifile->secl, ifile->path);
        ifile = ifile->next;
    }
    if (iuser != NULL)
    {
        sprintf(read_data, "%susers:", read_data);
        while (iuser != NULL)
        {
            sprintf(read_data, "%s%d%d:", read_data, iuser->secl, iuser->uid);
            iuser = iuser->next;
        }
    }

    sprintf(read_data, "%s\n", read_data);
    read_data_ptr = read_data;
	int count_cpy = count;
    while (count_cpy-- && *read_data_ptr)
        put_user(*(read_data_ptr++), user_buffer++);
    kfree(read_data);
    return count;
}

static ssize_t device_file_write(struct file *file_ptr, const char *user_buffer, size_t count, loff_t *position) {
    char *ptr;
    char input_buffer[MAX_PATH_LEN];
    int bytes_count_cpy = count;

    ptr = input_buffer;
    while (bytes_count_cpy--)
        get_user(*(ptr++), user_buffer++);

    if (input_buffer[0] == '0')
    {
        int uid;
        sscanf(input_buffer + 2, "%d", &uid);
        new_user(uid, input_buffer[1] - '0');
    }
    else if (input_buffer[0] == '1')
    {
        int i = 0, j = 0;
        for(; i < count && input_buffer[i] != '\n'; i++);
        	char * path = (char*) kmalloc((i - 1) * sizeof(char), GFP_KERNEL);
        for(; j < i - 2; j++)
            *(path + j) = input_buffer[2 + j];
        *(path + i - 2) = '\0';
        new_file(path, input_buffer[1] - '0');
    }
    return count;
}


static int device_file_open(struct inode *inode, struct file *file_ptr)
{
    if (open_count)
        return -EBUSY;
    open_count++;
    try_module_get(THIS_MODULE);
    return 0;
}

static int device_file_release(struct inode *inode, struct file *file_ptr)
{
    open_count--;
    module_put(THIS_MODULE);
    return 0;
}

static struct file_operations driver_ops = {
	.owner = THIS_MODULE,
	.read = device_file_read,
	.write = device_file_write,
	.open = device_file_open,
    .release = device_file_release
};

static int device_file_major_number = 0;
static const char device_name[] = "OS_phase2_driver";

int register_device(void) {
	printk( KERN_NOTICE "register_device() is called." );

	int result = 0;
	result = register_chrdev(0, device_name, &driver_ops);
	if (result < 0) {
		printk( KERN_WARNING "can\'t register character device with errorcode = %i", result);
		return result;
	}
	device_file_major_number = result;
	printk( KERN_NOTICE "registered device with major number = %i", result);
	
    sys_call_table = (sys_call_ptr_t *)kallsyms_lookup_name("sys_call_table");
    open_table = (custom_open)sys_call_table[__NR_open];

    write_cr0(read_cr0() & (~0x10000));
    sys_call_table[__NR_open] = (sys_call_ptr_t)open_syscall;
    write_cr0(read_cr0() | 0x10000);
    printk(KERN_INFO "open syscall was replaced successfully\n");
	return 0;
}

void unregister_device(void) {
	printk( KERN_NOTICE "unregister_device() is called.");
	
	if (device_file_major_number != 0)
		unregister_chrdev(device_file_major_number, device_name);
	
    user_entry * iuser = users;
    user_entry * temp_u;
    file_entry * ifile = files;
    file_entry * temp_f;
    while(iuser != NULL){
        temp_u = iuser;
        iuser = iuser->next;
        kfree(temp_u);
    }
    while(ifile != NULL){
        temp_f = ifile;
        ifile = ifile->next;
        kfree(temp_f);
    }
    write_cr0(read_cr0() & (~0x10000));
    sys_call_table[__NR_open] = (sys_call_ptr_t)open_table;    
    write_cr0(read_cr0() | 0x10000);
    printk(KERN_INFO "open syscall was replaced with the old one successfully\n");
}
