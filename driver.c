#include "device_file.h"
#include <linux/module.h>
#include <linux/init.h>


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Group \"Ya-Heidar-e-Karrar\"");

static int driver_init(void) {
    // needs to be filled
    int result = 0;
    printk(KERN_NOTICE "initialization start");
    result = register_device();
    return result;
}

static void driver_exit(void) {
    // needs to be filled
    printk(KERN_NOTICE "exiting");  
    unregister_device();
}

module_init(driver_init);
module_exit(driver_exit);
