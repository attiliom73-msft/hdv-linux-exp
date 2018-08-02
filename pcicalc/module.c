#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/pci.h>

//unsigned long copy_from_user(void*, void*, unsigned long);
//unsigned long copy_to_user(void*, const void*, unsigned long);

#define DEVICE_MAJOR_NUMBER 200
#define DEVICE_NAME "pcicalculator"
#define PCI_VENDOR_ID_MICROSOFT 0x1414
#define PCI_DEVICE_ID_CALCULATOR 0xabcd

int init_module(void);
void cleanup_module(void);
int device_open(struct inode*, struct file *);
int device_release(struct inode*, struct file *);
ssize_t device_read(struct file*, char*, size_t, loff_t*);
ssize_t device_write(struct file*, const char*, size_t, loff_t*);
int pci_probe (struct pci_dev*, const struct pci_device_id*);
void pci_remove(struct pci_dev*);

struct pci_device_id pcicalc_ids[] = {
    { PCI_DEVICE(0x1414, 0xabcd) },
    { 0 }
};

MODULE_DEVICE_TABLE(pci, pcicalc_ids);

struct pci_driver pci_driver = {
    .name = "pcicalc",
    .id_table = pcicalc_ids,
    .probe = pci_probe,
    .remove = pci_remove
};

struct dev_instance {
    void* bar0;
    void* buffer;
    unsigned long buffer_physical_address;
};

struct file_operations g_FileOps = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};

char* g_operation_result = NULL;
size_t g_operation_result_size = 0;
struct dev_instance g_dev = { 0 };

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
        goto cleanup;
    }
    
    result = pci_register_driver(&pci_driver);
    if (result < 0) {
        pr_alert("pci_register_driver failed: %d\n", result);
        goto cleanup;
    }    

cleanup:

    if (result < 0) {
        if (g_operation_result != NULL) {
            kfree(g_operation_result);
        }
        unregister_chrdev(DEVICE_MAJOR_NUMBER, DEVICE_NAME);
    }

    return result;
}

void cleanup_module(void)
{
    pci_unregister_driver(&pci_driver);    
    unregister_chrdev(DEVICE_MAJOR_NUMBER, DEVICE_NAME);
    kfree(g_operation_result);
    pr_info("module unloaded\n");
}

int pci_probe (struct pci_dev* dev, const struct pci_device_id* id)
{
    int result;
    unsigned long bar0_start;
    unsigned long bar0_end;

    pr_info("pci probe\n");
    
    if (id->vendor != PCI_VENDOR_ID_MICROSOFT ||
        id->device != PCI_DEVICE_ID_CALCULATOR)
    {
	    return -1;
	}
    
    pr_info("pci device found\n");
    
    result = pci_request_mem_regions(dev, "calc");
    if (result != 0) {
        pr_alert("pci_request_mem_regions failed: %d\n", result);
        return result;
    }
    
    bar0_start = pci_resource_start(dev, 0);
    bar0_end = pci_resource_end(dev, 0);
    pr_info("resource: BAR0 = %lx - %lx\n", bar0_start, bar0_end);
    
    g_dev.bar0 = pci_iomap(dev, 0, 4096);
    if (g_dev.bar0 == NULL) {
        pr_alert("pci_iomap failed\n");
        return -ENOMEM;
    }
    pr_info("BAR0 VA = %lx\n", (unsigned long)g_dev.bar0);
    
    g_dev.buffer = kmalloc(4096, GFP_DMA);
    if (g_dev.buffer == NULL) {
        pr_alert("kmalloc failed\n");
        return -ENOMEM;
    }
    pr_info("buffer VA = %lx\n", (unsigned long)g_dev.buffer);
    /*    
    result = mlock(g_dev.buffer, 4096);
    if (result < 0) {
        pr_alert("mlock failed\n");
        return -ENOMEM;
    }*/
    g_dev.buffer_physical_address = virt_to_phys(g_dev.buffer);
    pr_info("buffer physical address = %lx\n", g_dev.buffer_physical_address);        

    result = pci_enable_device(dev);
    if (result != 0) {
        pr_alert("pci_enable_device failed: %d\n", result);
        return result;
    }
    pr_info("pci device enabled\n");
    
    writeq((unsigned long)g_dev.buffer_physical_address, g_dev.bar0);
    
    return result;        
}

void pci_remove(struct pci_dev* dev)
{
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
