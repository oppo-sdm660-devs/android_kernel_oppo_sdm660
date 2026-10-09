// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-buf.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "ion.h"
#include "ion_legacy.h"

struct ion_legacy_handle {
	struct dma_buf *dmabuf;
	unsigned int users;
};

struct ion_legacy_client {
	/* Protects handles and their user counts. */
	struct mutex lock;
	struct idr handles;
};

int ion_legacy_open(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	mutex_init(&client->lock);
	idr_init(&client->handles);
	file->private_data = client;
	return 0;
}

int ion_legacy_release(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_handle *handle;
	int id;

	idr_for_each_entry(&client->handles, handle, id) {
		dma_buf_put(handle->dmabuf);
		kfree(handle);
	}
	idr_destroy(&client->handles);
	kfree(client);
	return 0;
}

struct dma_buf *ion_legacy_handle_get(struct file *file, int id)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_handle *handle;
	struct dma_buf *dmabuf = ERR_PTR(-EINVAL);

	mutex_lock(&client->lock);
	handle = idr_find(&client->handles, id);
	if (handle && id > 0) {
		dmabuf = handle->dmabuf;
		get_dma_buf(dmabuf);
	}
	mutex_unlock(&client->lock);
	return dmabuf;
}

static int ion_legacy_handle_add(struct file *file, struct dma_buf *dmabuf,
				 int __user *user_handle)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_handle *handle;
	bool existing = false;
	int id, ret = 0;

	mutex_lock(&client->lock);
	idr_for_each_entry(&client->handles, handle, id) {
		if (handle->dmabuf->priv == dmabuf->priv) {
			if (handle->users == UINT_MAX) {
				ret = -EOVERFLOW;
				goto out_unlock;
			}
			handle->users++;
			existing = true;
			goto copy_handle;
		}
	}

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	handle->dmabuf = dmabuf;
	handle->users = 1;
	id = idr_alloc(&client->handles, handle, 1, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		kfree(handle);
		goto out_unlock;
	}

copy_handle:
	if (put_user(id, user_handle)) {
		ret = -EFAULT;
		if (!--handle->users) {
			idr_remove(&client->handles, id);
			kfree(handle);
		}
	}
out_unlock:
	mutex_unlock(&client->lock);
	if (ret || existing)
		dma_buf_put(dmabuf);
	return ret;
}

int ion_legacy_alloc_ioctl(struct file *file, size_t len, size_t align,
			   unsigned int heap_id_mask, unsigned int flags,
			   int __user *user_handle)
{
	struct dma_buf *dmabuf;

	dmabuf = ion_alloc_dmabuf_aligned(len, align, heap_id_mask, flags);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);
	return ion_legacy_handle_add(file, dmabuf, user_handle);
}

static int ion_legacy_free_ioctl(struct file *file, int id)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_handle *handle;
	struct dma_buf *dmabuf = NULL;
	int ret = 0;

	/* Keep libion's v2 ABI probe. */
	if (!id)
		return -ENOTTY;

	mutex_lock(&client->lock);
	handle = idr_find(&client->handles, id);
	if (!handle || id < 0) {
		ret = -EINVAL;
	} else if (!--handle->users) {
		idr_remove(&client->handles, id);
		dmabuf = handle->dmabuf;
		kfree(handle);
	}
	mutex_unlock(&client->lock);
	if (dmabuf)
		dma_buf_put(dmabuf);
	return ret;
}

static int ion_legacy_share_ioctl(struct file *file,
				  struct ion_fd_data *data, void __user *arg)
{
	struct dma_buf *dmabuf;
	int fd;

	dmabuf = ion_legacy_handle_get(file, data->handle);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		goto out_put;

	data->fd = fd;
	if (copy_to_user(arg, data, sizeof(*data))) {
		put_unused_fd(fd);
		fd = -EFAULT;
		goto out_put;
	}
	fd_install(fd, dmabuf->file);
	return 0;

out_put:
	dma_buf_put(dmabuf);
	return fd;
}

long ion_legacy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *user_arg = (void __user *)arg;
	struct ion_fd_data data;
	struct dma_buf *dmabuf;

	switch (cmd) {
	case ION_OLD_IOC_ALLOC: {
		struct ion_old_allocation_data allocation;

		if (copy_from_user(&allocation, user_arg, sizeof(allocation)))
			return -EFAULT;
		return ion_legacy_alloc_ioctl(file, allocation.len,
				allocation.align, allocation.heap_id_mask,
				allocation.flags,
				&((struct ion_old_allocation_data __user *)
				  user_arg)->handle);
	}
	case ION_IOC_FREE: {
		struct ion_handle_data handle;

		if (copy_from_user(&handle, user_arg, sizeof(handle)))
			return -EFAULT;
		return ion_legacy_free_ioctl(file, handle.handle);
	}
	case ION_IOC_SHARE:
	case ION_IOC_MAP:
		if (copy_from_user(&data, user_arg, sizeof(data)))
			return -EFAULT;
		return ion_legacy_share_ioctl(file, &data, user_arg);
	case ION_IOC_IMPORT:
		if (copy_from_user(&data, user_arg, sizeof(data)))
			return -EFAULT;
		dmabuf = dma_buf_get(data.fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		if (!ion_legacy_buffer_is_ion(dmabuf)) {
			dma_buf_put(dmabuf);
			return -EINVAL;
		}
		return ion_legacy_handle_add(file, dmabuf,
				&((struct ion_fd_data __user *)user_arg)->handle);
	default:
		return -ENOTTY;
	}
}
