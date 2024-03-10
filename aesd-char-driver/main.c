/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Asaf Gery");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
  PDEBUG("open");
  struct aesd_dev *dev; /* device information */

  dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
  filp->private_data = dev; /* for other methods */
  return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
  PDEBUG("release");
  return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos)
{
  ssize_t retval = 0;
  PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
  struct aesd_dev *dev = filp->private_data;
  struct aesd_circular_buffer *buffer = dev->buffer;
  size_t entry_offset_byte;
  PDEBUG("read acquiring lock");
  if (mutex_lock_interruptible(&(dev->lock))) {
    PDEBUG("read failed to acquire lock");
    return -ERESTARTSYS;
  }
  struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(
      buffer, *f_pos, &entry_offset_byte);
  if (entry) {
    PDEBUG("read found entry at %p, offset inside entry: %zu", 
        entry, entry_offset_byte);
    size_t entry_bytes_to_read = entry.size - entry_offset_byte;
    if (count > entry_bytes_to_read) {
      count = entry_bytes_to_read;
      PDEBUG("read count trimmed to %zu", count);
    }
    if (copy_to_user(buf, entry + entry_offset_byte, count)) {
      retval = -EFAULT;
      PDEBUG("read copy_to_user() failed");
      goto fail;
    }
    *f_pos += count;
    retval = count;
  }
fail:
  mutex_unlock(&(dev->lock));
  PDEBUG("read before exiting retval: %zd, *f_pos: %lld", retval, *f_pos);
  return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
    loff_t *f_pos)
{
  ssize_t retval = -ENOMEM;
  PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
  struct aesd_dev *dev = filp->private_data;
  struct aesd_buffer_entry *entry = dev->entry;
  struct aesd_circular_buffer *buffer = dev->buffer;
  if (mutex_lock_interruptible(&(dev->lock))) {
    return -ERESTARTSYS;
  }
  if (entry.size == 0) {
    PDEBUG("write entry.size == 0, allocating %zu bytes for entry.buffptr", count);
    entry.buffptr = kzalloc(sizeof(char) * count, GFP_KERNEL);
  } 
  else {
    PDEBUG("write entry.size == %zu, reallocating %zu bytes for entry.buffptr", 
        entry.size, entry.size + count);
    entry.buffptr = krealloc_array(entry.buffptr, entry.size + count, sizeof(char), GFP_KERNEL);
  }
  if (!entry.buffptr) {
    goto fail;
  }
  if (copy_from_user(entry.buffptr, buf, count)) {
    PDEBUG("write copy_from_user() failed"); 
    retval = -EFAULT;
    goto fail;
  }
  entry.size += count;
  // if command is terminated, add to circular buffer
  if (entry.buffptr[entry.size - 1] == '\n') {
    PDEBUG("write entry is complete, adding to circular buffer"); 
    const char *oldbuffptr = aesd_circular_buffer_add_entry(buffer, entry);
    if (oldbuffptr) {
      PDEBUG("write freeing oldbuffptr returned from aesd_circular_buffer_add_entry()"); 
      kfree(oldbuffptr);
    }
    // reset for next write
    entry.size = 0;
    entry.buffptr = NULL;
  }
  retval = count;
  *f_pos += count;
fail:
  mutex_unlock(&(dev->lock));
  return retval;
}

struct file_operations aesd_fops = {
  .owner =    THIS_MODULE,
  .read =     aesd_read,
  .write =    aesd_write,
  .open =     aesd_open,
  .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
  int err, devno = MKDEV(aesd_major, aesd_minor);

  cdev_init(&dev->cdev, &aesd_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &aesd_fops;
  err = cdev_add (&dev->cdev, devno, 1);
  if (err) {
    printk(KERN_ERR "Error %d adding aesd cdev", err);
  }
  return err;
}

int aesd_init_module(void)
{
  dev_t dev = 0;
  int result;
  result = alloc_chrdev_region(&dev, aesd_minor, 1,
      "aesdchar");
  aesd_major = MAJOR(dev);
  if (result < 0) {
    printk(KERN_WARNING "Can't get major %d\n", aesd_major);
    return result:
  }
  memset(&aesd_device,0,sizeof(struct aesd_dev));
  aesd_device.entry = kzalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
  if (!aesd_device.entry) {
    printk(KERN_ERR "Can't allocate memory for aesd char device\n");
    result = -ENOMEM;
    goto fail;
  }
  aesd_device.buffer = kzalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
  if (!aesd_device.buffer) {
    printk(KERN_ERR "Can't allocate memory for aesd char device\n");
    result = -ENOMEM;
    goto fail;
  }
  mutex_init(&(aesd_device.lock));
  result = aesd_setup_cdev(&aesd_device);
  if (!result) {
    return 0;
  }
fail:
  if (aesd_device.buffer) {
    kfree(aesd_device.buffer);
  }
  if (aesd_device.entry) {
    kfree(aesd_device.entry);
  }
  unregister_chrdev_region(dev, 1);
  return result;

}

void aesd_cleanup_module(void)
{
  dev_t devno = MKDEV(aesd_major, aesd_minor);

  cdev_del(&aesd_device.cdev);
  struct aesd_buffer_entry *entry = aesd_device.entry;
  struct aesd_circular_buffer *buffer = aesd_device.buffer;
  if (entry) {
    if (entry.buffptr) {
      kfree(entry.buffptr);
    }
    kfree(entry);
  }
  if (buffer) {
    int i;
    // reuse entry pointer to run on circular buffer entries
    AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, i) {
      if (entry.buffptr) {
        kfree(entry.buffptr);
      }
    }
    kfree(buffer);
  }
  mutex_destroy(&(aesd_device.lock));
  unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
