#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>

//unsigned long copy_from_user(void*, void*, unsigned long);
//unsigned long copy_to_user(void*, const void*, unsigned long);

#define DEVICE_MAJOR_NUMBER 200
#define DEVICE_NAME "pcicalculator"

int init_module(void);
void cleanup_module(void);
int device_open(struct inode*, struct file *);
int device_release(struct inode*, struct file *);
ssize_t device_read(struct file*, char*, size_t, loff_t*);
ssize_t device_write(struct file*, const char*, size_t, loff_t*);

struct file_operations g_FileOps = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};

char* g_operation_result = NULL;
size_t g_operation_result_size = 0;

int init_module(void)
{
    int result;
	
    pr_info("module loading\n");
	
    g_operation_result = (char*)kmalloc(4096, GFP_KERNEL);
    if (g_operation_result == NULL) {
        pr_alert("kmalloc failed\n");
        result = -ENOMEM;
        goto cleanup;		
    }
    memcpy(g_operation_result, "zero\n", 5);
    g_operation_result_size = 5;

    result = register_chrdev(DEVICE_MAJOR_NUMBER, DEVICE_NAME, &g_FileOps);
    if (result < 0) {
        pr_alert("register_chrdev failed: %d\n", result);
        return result;
    }	

cleanup:

    if (result < 0 && g_operation_result != NULL) {
        kfree(g_operation_result);
    }

    return result;
}

void cleanup_module(void)
{
    unregister_chrdev(DEVICE_MAJOR_NUMBER, DEVICE_NAME);
    kfree(g_operation_result);
    pr_info("module unloaded\n");
}

int device_open(struct inode* inode, struct file* file)
{
    pr_info("device_open\n");
    try_module_get(THIS_MODULE);
    return 0;
}

int device_release(struct inode* inode, struct file* file)
{
    pr_info("device_release\n");
    module_put(THIS_MODULE);
    return 0;
}

ssize_t device_read(struct file* file, char* buffer, size_t length, loff_t* offset)
{
    size_t bytes_read;	
	
    pr_info("device_read: length = %lu, offset = %lld\n", length, *offset);

    bytes_read = length;
    if (*offset + length > g_operation_result_size) {
        bytes_read = g_operation_result_size - *offset;
    }
    if (bytes_read > 0) {
        if (copy_to_user(buffer, g_operation_result + *offset, bytes_read)) {
            return -EFAULT;
        }
        *offset = *offset + bytes_read;
    }	
	
    return bytes_read;	
}

ssize_t device_write(struct file* file, const char* buffer, size_t length, loff_t* offset)
{
    ssize_t bytes_written;
    char* input = NULL;
    char* arg1_str = NULL;
    char* arg2_str = NULL;
    long arg1;
    long arg2;
    long result;
    size_t i;

    pr_info("device_write: length = %lu, offset = %lld\n", length, *offset);
	
    input = (char*)kmalloc(length, GFP_KERNEL);
    if (input == NULL) {
        pr_alert("kmalloc failed\n");
        bytes_written = -ENOMEM;
        goto cleanup;		
    }
    if (copy_from_user(input, buffer, length)) {
        pr_alert("copy_from_user failed\n");
        bytes_written = -EFAULT;
        goto cleanup;	
    }
		
    for (i = 0; i < length; ++i) {
        if (arg2_str == NULL && input[i] == '+') {
            pr_alert("found operator at index %ld\n", i);
            input[i] = '\0';
            arg2_str = input + i + 1;
        } else if (input[i] < '0' || input[i] > '9') {
            pr_alert("found terminator at index %ld\n", i);
            input[i] = '\0';
            arg1_str = input;
            break;
        }
    }
	
    if (arg1_str == NULL || arg2_str == NULL) {
        pr_alert("invalid args\n");
        bytes_written = -EINVAL;
        goto cleanup;
    }
	
    if (kstrtol(arg1_str, 10, &arg1) != 0) {
        pr_alert("invalid arg1\n");
        bytes_written = -EINVAL;
        goto cleanup;		
    }
    if (kstrtol(arg2_str, 10, &arg2) != 0) {
        pr_alert("invalid arg2\n");
        bytes_written = -EINVAL;
        goto cleanup;		
    }
    result = arg1 + arg2;
    g_operation_result_size = snprintf(g_operation_result, 4096, "%ld\n", result);

    bytes_written = length;	
	
cleanup:

    if (input != NULL) {
        kfree(input);
    }

    return bytes_written;
}

MODULE_LICENSE("GPL");
