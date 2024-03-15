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
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Asaf Gery");
MODULE_LICENSE("Dual BSD/GPL");

#ifdef AESD_DEBUG
#define DEBUG_BYTE 1 // add extra byte for '\0' when allocating memory
                     // for cmd read from user space.
                     // This allows printing the buffer's content with
                     // printk easily.
#else
#define DEBUG_BYTE 0
#endif
struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp) {
  struct aesd_dev *dev; /* device information */
  PDEBUG("open");
  dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
  filp->private_data = dev; /* for other methods */
  return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
  PDEBUG("release");
  return 0;
}

ssize_t aesd_read(
    struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
  ssize_t retval = 0;
  struct aesd_dev *dev;
  struct aesd_circular_buffer *buffer;
  struct aesd_buffer_entry *entry;
  size_t entry_offset_byte;
  PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
  dev = filp->private_data;
  buffer = dev->buffer;
  PDEBUG("read acquiring lock");
  if (mutex_lock_interruptible(&(dev->lock))) {
    PDEBUG("read failed to acquire lock");
    return -ERESTARTSYS;
  }
  entry = aesd_circular_buffer_find_entry_offset_for_fpos(
      buffer, *f_pos, &entry_offset_byte);
  if (entry) {
    size_t entry_bytes_to_read = entry->size - entry_offset_byte;
    PDEBUG("read found entry at %p, offset inside entry: %zu", 
        entry, entry_offset_byte);
    if (count > entry_bytes_to_read) {
      count = entry_bytes_to_read;
      PDEBUG("read count trimmed to %zu", count);
    }
    if (copy_to_user(buf, entry->buffptr + entry_offset_byte, count)) {
      retval = -EFAULT;
      PDEBUG("read copy_to_user() failed");
      goto fail;
    }
    PDEBUG("copied to user %zu bytes from \"%s\"", count, entry->buffptr + entry_offset_byte);
    *f_pos += count;
    retval = count;
  }
fail:
  PDEBUG("read releasing lock");
  mutex_unlock(&(dev->lock));
  PDEBUG("read before exiting retval: %zd, *f_pos: %lld", retval, *f_pos);
  return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
    loff_t *f_pos) {
  ssize_t retval = -ENOMEM;
  struct aesd_circular_buffer *buffer;
  struct aesd_dev *dev = filp->private_data;
  PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
  buffer = dev->buffer;
  PDEBUG("write acquiring lock");
  if (mutex_lock_interruptible(&(dev->lock))) {
    return -ERESTARTSYS;
  }
  if (dev->tmp_buff_size == 0) {
    PDEBUG("write dev->tmp_buff_size == 0, allocating %zu bytes for dev->tmp_buff_ptr", count);
    dev->tmp_buff_ptr = kzalloc(sizeof(char) * (count + DEBUG_BYTE), GFP_KERNEL);
  } 
  else {
    PDEBUG("write dev->tmp_buff_size: %zu, reallocating %zu bytes for dev->tmp_buff_ptr", 
        dev->tmp_buff_size, dev->tmp_buff_size + count);
    dev->tmp_buff_ptr = krealloc_array(
        dev->tmp_buff_ptr, dev->tmp_buff_size + count + DEBUG_BYTE, sizeof(char), GFP_KERNEL);
  }
  if (!dev->tmp_buff_ptr) {
    goto fail;
  }
  if (copy_from_user(dev->tmp_buff_ptr + dev->tmp_buff_size, buf, count)) {
    PDEBUG("write copy_from_user() failed"); 
    retval = -EFAULT;
    goto fail;
  }
  dev->tmp_buff_size += count;
#ifdef AESD_DEBUG
  // in debug mode we add extra byte to terminate the string with '\0'
  // to allow printing its content
  dev->tmp_buff_ptr[dev->tmp_buff_size] = '\0';
  PDEBUG("write dev->tmp_buff_ptr: \"%s\"", dev->tmp_buff_ptr);
#endif
  // if command is terminated, add to circular buffer
  if (dev->tmp_buff_ptr[dev->tmp_buff_size - 1] == '\n') {
    const struct aesd_buffer_entry entry = {
      .buffptr = dev->tmp_buff_ptr,
      .size = dev->tmp_buff_size,
    };
    const char *oldbuffptr = aesd_circular_buffer_add_entry(buffer, &entry);
    PDEBUG("write entry is complete, added to circular buffer"); 
    if (oldbuffptr) {
      PDEBUG("write freeing oldbuffptr returned from aesd_circular_buffer_add_entry()"); 
      kfree(oldbuffptr);
    }
    // reset for next write
    dev->tmp_buff_size = 0;
    dev->tmp_buff_ptr = NULL;
  }
  retval = count;
  *f_pos += count;
  PDEBUG("write written %zu bytes, current offset %lld", count, *f_pos);
fail:
  PDEBUG("write releasing lock");
  mutex_unlock(&(dev->lock));
  PDEBUG("write returning value: %zd", retval);
  return retval;
}

static loff_t get_buffer_size(const struct aesd_circular_buffer *buffer) {
  int i;
  const struct aesd_buffer_entry *entry;
  loff_t size = 0;
  for (i = 0;i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;i++) {
    entry = &(buffer->entry[i]);
    size += entry->size;
  }
  return size;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence) {
  loff_t buff_size, retval;
  struct aesd_dev *dev = filp->private_data;

  if (mutex_lock_interruptible(&(dev->lock))) {
    PDEBUG("llseek failed to acquire lock");
    return -ERESTARTSYS;
  }
  buff_size = get_buffer_size((const struct aesd_circular_buffer *)dev->buffer);
  retval = fixed_size_llseek(filp, off, whence, buff_size);
  PDEBUG("fixed_size_llseek returned %lld", retval);
  PDEBUG("llseek releasing lock");
  mutex_unlock(&(dev->lock));
  return retval;
}

static long aesd_adjust_file_offset(
    struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset) {
  int i;
  long retval;
  loff_t offset, buff_size;
  struct aesd_buffer_entry *entry;
  struct aesd_circular_buffer *buffer;
  struct aesd_dev *dev = filp->private_data;
  if (write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
    PDEBUG("aesd_adjust_file_offset error: write_cmd (%u) is out of range", write_cmd);
    return -EINVAL;
  }
  if (mutex_lock_interruptible(&(dev->lock))) {
    PDEBUG("aesd_adjust_file_offset failed to acquire lock");
    return -ERESTARTSYS;
  }
  buffer = dev->buffer;
  entry = &(buffer->entry[write_cmd]);
  if (write_cmd_offset >= entry->size) {
    PDEBUG("aesd_adjust_file_offset error: write_cmd_offset (%u) exceeds cmd size (%lu)",
        write_cmd_offset, entry->size);
    retval = -EINVAL;
    goto out;
  }
  buff_size = get_buffer_size(buffer);
  offset = 0;
  for (i = 0;i < write_cmd;i++) {
    entry = &(buffer->entry[i]);
    offset += entry->size;
  }
  offset += write_cmd_offset;
  retval = fixed_size_llseek(filp, offset, SEEK_SET, buff_size);
  if (retval >= 0) { // we are not interested in the offset,
                     // just want to know that the function succeeded
    retval = 0;
  }
out:
  PDEBUG("aesd_adjust_file_offset releasing lock");
  mutex_unlock(&(dev->lock));
  PDEBUG("aesd_adjust_file_offset returning %ld", retval);
  return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
  int retval;
  struct aesd_seekto seek_to;
  /*
   * extract the type and number bitfields, and don't decode
   * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
   */
  if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
  if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;
  /*
   * the direction is a bitmask, and VERIFY_WRITE catches R/W
   * transfers. `Type' is user-oriented, while
   * access_ok is kernel-oriented, so the concept of "read" and
   * "write" is reversed
   */
  if (_IOC_DIR(cmd) & _IOC_READ || _IOC_DIR(cmd) & _IOC_WRITE) {
    if(!access_ok((void __user *)arg, _IOC_SIZE(cmd))) {
      return -EFAULT;
    }
  }
  switch(cmd) {
    case AESDCHAR_IOCSEEKTO:
      if (copy_from_user(&seek_to, (const void __user *)arg, sizeof(struct aesd_seekto))) {
        retval = -EFAULT;
      }
      else {
        retval = aesd_adjust_file_offset(
            filp, seek_to.write_cmd, seek_to.write_cmd_offset);
      }
      break;
    default:
      retval = -ENOTTY;
  }
  return retval;
}

struct file_operations aesd_fops = {
  .owner   = THIS_MODULE,
  .llseek  = aesd_llseek,
  .read    = aesd_read,
  .write   = aesd_write,
  .open    = aesd_open,
  .release = aesd_release,
  .unlocked_ioctl = aesd_ioctl,
};


static int aesd_setup_cdev(struct aesd_dev *dev) {
  int err, devno = MKDEV(aesd_major, aesd_minor);

  cdev_init(&dev->cdev, &aesd_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &aesd_fops;
  err = cdev_add (&dev->cdev, devno, 1);
  if (err) {
    printk(KERN_ERR "Error %d adding aesd cdev\n", err);
  }
  return err;
}

int aesd_init_module(void) {
  dev_t dev = 0;
  int result;
  result = alloc_chrdev_region(&dev, aesd_minor, 1,
      "aesdchar");
  aesd_major = MAJOR(dev);
  if (result < 0) {
    printk(KERN_WARNING "Can't get major %d\n", aesd_major);
    return result;
  }
  memset(&aesd_device,0,sizeof(struct aesd_dev));
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
  unregister_chrdev_region(dev, 1);
  return result;

}

void aesd_cleanup_module(void) {
  char *tmp_buff_ptr;
  struct aesd_buffer_entry *entry;
  struct aesd_circular_buffer *buffer;
  dev_t devno = MKDEV(aesd_major, aesd_minor);

  cdev_del(&aesd_device.cdev);
  tmp_buff_ptr = aesd_device.tmp_buff_ptr;
  buffer = aesd_device.buffer;
  if (aesd_device.tmp_buff_size > 0) {
    kfree(tmp_buff_ptr);
  }
  if (buffer) {
    int i;
    // reuse entry pointer to run on circular buffer entries
    AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, i) {
      if (entry->buffptr) {
        kfree(entry->buffptr);
      }
    }
    kfree(buffer);
  }
  mutex_destroy(&(aesd_device.lock));
  unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
