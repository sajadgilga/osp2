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
#define wnum write_num_to_string_arr

// type definitions

typedef struct User
{
    int uid;
    int sl;
    struct User *next;
} user_entry;
typedef struct File
{
    char *path;
    int sl;
    struct File *next;
} file_entry;
typedef asmlinkage long (*custom_open) (const char __user *filename, int flags, umode_t mode);
typedef asmlinkage long (*sys_call_ptr_t)(const struct pt_regs *);

// variable definitions

static char requested_data[1000];
static int data_size = 1000;
static sys_call_ptr_t *sys_call_table;
custom_open open_table;
static user_entry *users = NULL;
static file_entry *files = NULL;
static int users_count = 0;
static int files_count = 0;
static int isOpen = 0;

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

void new_user(int uid, int sl)
{
    user_entry *entry = find_user_entry(uid);
    if(entry != NULL){
        entry->sl = sl;
        return;
    }
    entry = (user_entry*) kmalloc(sizeof(user_entry), GFP_KERNEL);
    if (entry == NULL)
    {
        printk(KERN_INFO "could not allocate new user entry due to error");
        return;
    }
    entry->uid = uid;
    entry->sl = sl;
    entry->next = users;
    users = entry;
    printk(KERN_INFO "new user entry added - uid: %d\nsl: %d\n", ++users_count, uid, sl);
}

void new_file(char *path, int sl)
{   
    file_entry *entry = find_file_entry(path);
    if(entry != NULL){
        entry->sl = sl;
        return;
    }
    entry = (file_entry*) kmalloc(sizeof(file_entry), GFP_KERNEL);
    if (entry == NULL)
    {
        printk(KERN_INFO "could not allocate new file entry due to error");
        return;
    }
    entry->sl = sl;
    entry->path = path;
    entry->next = files;
    files = entry;
    printk(KERN_INFO "new file entry added - path:%s\nsl: %d\n", ++files_count, path, sl);
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

static asmlinkage long open_syscall(const char __user *filename, int flags, umode_t mode)
{
    user_entry * iuser;
    file_entry * ifile;
    int current_user_id = (int) get_current_user()->uid.val;
	
	printk(KERN_INFO "user %d wants to open a file\n", current_user_id);
    int user_sl = 0, file_sl = 0;

    char kfilename[256];
	kfilename = filename;
    char *ptr = kfilename, *buffer = filename;
    int bytes_count_cpy = 256;
    while (bytes_count_cpy--){
        get_user(*ptr, buffer++);
        if(*(ptr++) == '\0')
            break;
    }

    int write_only = flags & O_WRONLY;
    int read_only = flags & O_RDONLY;
	int read_write = flags & O_RDWR;
	
    if((iuser = find_user_entry(current_user_id))!= NULL)
        user_sl = iuser->sl;

    if((ifile = find_file_entry(kfilename))!= NULL){
        file_sl = ifile->sl;
    }

    if (user_sl == file_sl)
		return open_table(filename, flags, mode);
    
    if( user_sl < file_sl) {
        if(write_only){
        	printk(KERN_INFO "write accepted\n");
            return open_table(filename, flags, mode);
		}
        printk(KERN_INFO "can not read\n");
        return -1;
    } else {
        if(read_only){
        	printk(KERN_INFO "read accepted\n");
            return open_table(filename, flags, mode);
		}
        printk(KERN_INFO "can not write\n");
        return -1;
    }
    
    return open_table(filename, flags, mode);
}


static ssize_t device_file_read(struct file *file_ptr, char __user *user_buffer, size_t count, loff_t *position) {
	printk( KERN_NOTICE "device file is read at offset = %i, read byters count = %u", (int)*position, (unsigned int) count);	
	
	if (*position >= data_size)
		return 0;
	if (count + *position > data_size)
		count = data_size - *position;
	copy_to_user(user_buffer, requested_data + *position, count);

	*position += count;
	return count;
}

static ssize_t device_file_write(struct file *file_ptr, const char *user_buffer, size_t count, loff_t *position) {
    char *ptr;
    char input_buffer[259];
    int bytes_count_cpy = count;

    ptr = input_buffer;
    while (bytes_count_cpy--)
        get_user(*(ptr++), user_buffer++);

    if (input_buffer[0] == '0')
    {
        int uid;
        sscanf(input_buffer + 2, "%d", &uid);
        new_user(uid, input_buffer[1]);
    }
    else if (input_buffer[0] == '1')
    {
        int i = 0, j = 0;
        for(i = 0; i < count && input_buffer[i] != '\n'; i++);
		char *path = (char*) kmalloc((i) * sizeof(char), GFP_KERNEL);
        for(j = 0; j < i - 2; j++)
            path[j] = input_buffer[2 + j];
        path[i - 2] = '\0';
        new_file(path, input_buffer[1]);
    }
    return count;
}


static int device_file_open(struct inode *inode, struct file *file_ptr)
{
    if (isOpen)
      return -EBUSY;
    try_module_get(THIS_MODULE);
    isOpen = 1;
	return 0;
}

static int device_file_release(struct inode *inode, struct file *file_ptr)
{
    module_put(THIS_MODULE);
    isOpen = 0;
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
    open_table = (custom_open) sys_call_table[__NR_open];
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
    while(iuser != NULL){
		user_entry *temp = iuser;
        iuser = iuser->next;
		kfree(temp);
    }

    file_entry * ifile = files;
    while(ifile != NULL){
		file_entry *temp = ifile;
        ifile = ifile->next;
		kfree(ifile);
    }

    write_cr0(read_cr0() & (~0x10000));
	
    sys_call_table[__NR_open] = (sys_call_ptr_t)open_table;    
    write_cr0(read_cr0() | 0x10000);
    printk(KERN_INFO "open syscall was replaced with the old one successfully\n");
}
