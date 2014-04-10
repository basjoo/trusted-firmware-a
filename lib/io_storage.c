/*
 * Copyright (c) 2014, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <stddef.h>
#include <assert.h>
#include <io_storage.h>
#include <io_driver.h>


#define MAX_DEVICES(plat_data)						\
	(sizeof((plat_data)->devices)/sizeof((plat_data)->devices[0]))


/* Storage for a fixed maximum number of IO entities, definable by platform */
static io_entity_t entity_pool[MAX_IO_HANDLES];

/* Simple way of tracking used storage - each entry is NULL or a pointer to an
 * entity */
static io_entity_t *entity_map[MAX_IO_HANDLES];

/* Track number of allocated entities */
static unsigned int entity_count;


/* Used to keep a reference to platform-specific data */
static io_plat_data_t *platform_data;


#if DEBUG	/* Extra validation functions only used in debug builds */

/* Return a boolean value indicating whether a device connector is valid */
static int is_valid_dev_connector(const io_dev_connector_t *dev_con)
{
	int result = (dev_con != NULL) && (dev_con->dev_open != NULL);
	return result;
}


/* Return a boolean value indicating whether a device handle is valid */
static int is_valid_dev(io_dev_handle handle)
{
	const io_dev_info_t *dev = handle;
	int result = (dev != NULL) && (dev->funcs != NULL) &&
			(dev->funcs->type != NULL) &&
			(dev->funcs->type() < IO_TYPE_MAX);
	return result;
}


/* Return a boolean value indicating whether an IO entity is valid */
static int is_valid_entity(io_handle handle)
{
	const io_entity_t *entity = handle;
	int result = (entity != NULL) && (is_valid_dev(entity->dev_handle));
	return result;
}


/* Return a boolean value indicating whether a seek mode is valid */
static int is_valid_seek_mode(io_seek_mode_t mode)
{
	return ((mode != IO_SEEK_INVALID) && (mode < IO_SEEK_MAX));
}

#endif	/* End of debug-only validation functions */


/* Open a connection to a specific device */
static int dev_open(const io_dev_connector_t *dev_con, void *dev_spec,
		io_dev_info_t **dev_info)
{
	int result = IO_FAIL;
	assert(dev_info != NULL);
	assert(is_valid_dev_connector(dev_con));

	result = dev_con->dev_open(dev_spec, dev_info);
	return result;
}


/* Set a handle to track an entity */
static void set_handle(io_handle *handle, io_entity_t *entity)
{
	assert(handle != NULL);
	*handle = entity;
}


/* Locate an entity in the pool, specified by address */
static int find_first_entity(struct io_entity *entity, unsigned int *index_out)
{
	int result = IO_FAIL;
	for (int index = 0; index < MAX_IO_HANDLES; ++index) {
		if (entity_map[index] == entity) {
			result = IO_SUCCESS;
			*index_out = index;
			break;
		}
	}
	return result;
}


/* Allocate an entity from the pool and return a pointer to it */
static int allocate_entity(struct io_entity **entity)
{
	int result = IO_FAIL;
	assert(entity != NULL);

	if (entity_count < MAX_IO_HANDLES) {
		unsigned int index = 0;
		result = find_first_entity(NULL, &index);
		assert(result == IO_SUCCESS);
		*entity = entity_map[index] = &entity_pool[index];
		++entity_count;
	} else
		result = IO_RESOURCES_EXHAUSTED;

	return result;
}


/* Release an entity back to the pool */
static int free_entity(struct io_entity *entity)
{
	int result = IO_FAIL;
	unsigned int index = 0;
	assert(entity != NULL);

	result = find_first_entity(entity, &index);
	if (result ==  IO_SUCCESS) {
		entity_map[index] = NULL;
		--entity_count;
	}

	return result;
}


/* Exported API */


/* Initialise the IO layer */
void io_init(struct io_plat_data *data)
{
	assert(data != NULL);
	platform_data = data;
}


/* Register a device driver */
int io_register_device(struct io_dev_info *dev_info)
{
	int result = IO_FAIL;
	assert(dev_info != NULL);
	assert(platform_data != NULL);

	unsigned int dev_count = platform_data->dev_count;

	if (dev_count < MAX_DEVICES(platform_data)) {
		platform_data->devices[dev_count] = dev_info;
		platform_data->dev_count++;
		result = IO_SUCCESS;
	} else {
		result = IO_RESOURCES_EXHAUSTED;
	}

	return result;
}


/* Open a connection to an IO device */
int io_dev_open(struct io_dev_connector *dev_con, void *dev_spec,
		io_dev_handle *handle)
{
	int result = IO_FAIL;
	assert(handle != NULL);

	result = dev_open(dev_con, dev_spec, handle);
	return result;
}


/* Initialise an IO device explicitly - to permit lazy initialisation or
 * re-initialisation */
int io_dev_init(struct io_dev_info *dev_handle, const void *init_params)
{
	int result = IO_FAIL;
	assert(dev_handle != NULL);
	assert(is_valid_dev(dev_handle));

	io_dev_info_t *dev = dev_handle;

	if (dev->funcs->dev_init != NULL) {
		result = dev->funcs->dev_init(dev, init_params);
	} else {
		/* Absence of registered function implies NOP here */
		result = IO_SUCCESS;
	}
	return result;
}


/* TODO: Consider whether an explicit "shutdown" API should be included */

/* Close a connection to a device */
int io_dev_close(io_dev_handle dev_handle)
{
	int result = IO_FAIL;
	assert(dev_handle != NULL);
	assert(is_valid_dev(dev_handle));

	io_dev_info_t *dev = dev_handle;

	if (dev->funcs->dev_close != NULL) {
		result = dev->funcs->dev_close(dev);
	} else {
		/* Absence of registered function implies NOP here */
		result = IO_SUCCESS;
	}

	return result;
}


/* Synchronous operations */


/* Open an IO entity */
int io_open(io_dev_handle dev_handle, const void *spec, io_handle *handle)
{
	int result = IO_FAIL;
	assert((spec != NULL) && (handle != NULL));
	assert(is_valid_dev(dev_handle));

	io_dev_info_t *dev = dev_handle;
	io_entity_t *entity;

	result = allocate_entity(&entity);

	if (result == IO_SUCCESS) {
		assert(dev->funcs->open != NULL);
		result = dev->funcs->open(dev, spec, entity);

		if (result == IO_SUCCESS) {
			entity->dev_handle = dev_handle;
			set_handle(handle, entity);
		} else
			free_entity(entity);
	}
	return result;
}


/* Seek to a specific position in an IO entity */
int io_seek(io_handle handle, io_seek_mode_t mode, ssize_t offset)
{
	int result = IO_FAIL;
	assert(is_valid_entity(handle) && is_valid_seek_mode(mode));

	io_entity_t *entity = handle;

	io_dev_info_t *dev = entity->dev_handle;

	if (dev->funcs->seek != NULL)
		result = dev->funcs->seek(entity, mode, offset);
	else
		result = IO_NOT_SUPPORTED;

	return result;
}


/* Determine the length of an IO entity */
int io_size(io_handle handle, size_t *length)
{
	int result = IO_FAIL;
	assert(is_valid_entity(handle) && (length != NULL));

	io_entity_t *entity = handle;

	io_dev_info_t *dev = entity->dev_handle;

	if (dev->funcs->size != NULL)
		result = dev->funcs->size(entity, length);
	else
		result = IO_NOT_SUPPORTED;

	return result;
}


/* Read data from an IO entity */
int io_read(io_handle handle, void *buffer, size_t length, size_t *length_read)
{
	int result = IO_FAIL;
	assert(is_valid_entity(handle) && (buffer != NULL));

	io_entity_t *entity = handle;

	io_dev_info_t *dev = entity->dev_handle;

	if (dev->funcs->read != NULL)
		result = dev->funcs->read(entity, buffer, length, length_read);
	else
		result = IO_NOT_SUPPORTED;

	return result;
}


/* Write data to an IO entity */
int io_write(io_handle handle, const void *buffer, size_t length,
		size_t *length_written)
{
	int result = IO_FAIL;
	assert(is_valid_entity(handle) && (buffer != NULL));

	io_entity_t *entity = handle;

	io_dev_info_t *dev = entity->dev_handle;

	if (dev->funcs->write != NULL) {
		result = dev->funcs->write(entity, buffer, length,
				length_written);
	} else
		result = IO_NOT_SUPPORTED;

	return result;
}


/* Close an IO entity */
int io_close(io_handle handle)
{
	int result = IO_FAIL;
	assert(is_valid_entity(handle));

	io_entity_t *entity = handle;

	io_dev_info_t *dev = entity->dev_handle;

	if (dev->funcs->close != NULL)
		result = dev->funcs->close(entity);
	else {
		/* Absence of registered function implies NOP here */
		result = IO_SUCCESS;
	}
	/* Ignore improbable free_entity failure */
	(void)free_entity(entity);

	return result;
}
