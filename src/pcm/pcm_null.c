/*
 *  PCM - Null plugin
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <byteswap.h>
#include <limits.h>
#include <sys/shm.h>
#include "pcm_local.h"
#include "pcm_plugin.h"

typedef struct {
	snd_timestamp_t trigger_time;
	int state;
	int shmid;
	snd_pcm_uframes_t appl_ptr;
	snd_pcm_uframes_t hw_ptr;
	int poll_fd;
} snd_pcm_null_t;

static int snd_pcm_null_close(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	close(null->poll_fd);
	free(null);
	return 0;
}

static int snd_pcm_null_card(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	return -ENOENT;	/* not available */
}

static int snd_pcm_null_nonblock(snd_pcm_t *pcm ATTRIBUTE_UNUSED, int nonblock ATTRIBUTE_UNUSED)
{
	return 0;
}

static int snd_pcm_null_async(snd_pcm_t *pcm ATTRIBUTE_UNUSED, int sig ATTRIBUTE_UNUSED, pid_t pid ATTRIBUTE_UNUSED)
{
	return -ENOSYS;
}

static int snd_pcm_null_info(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_info_t * info)
{
	memset(info, 0, sizeof(*info));
	/* FIXME */
	return 0;
}

static int snd_pcm_null_channel_info(snd_pcm_t *pcm, snd_pcm_channel_info_t * info)
{
	snd_pcm_null_t *null = pcm->private;
	return snd_pcm_channel_info_shm(pcm, info, null->shmid);
}

static int snd_pcm_null_status(snd_pcm_t *pcm, snd_pcm_status_t * status)
{
	snd_pcm_null_t *null = pcm->private;
	memset(status, 0, sizeof(*status));
	status->state = null->state;
	status->trigger_time = null->trigger_time;
	gettimeofday(&status->tstamp, 0);
	status->avail = pcm->buffer_size;
	status->avail_max = status->avail;
	return 0;
}

static int snd_pcm_null_state(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	return null->state;
}

static int snd_pcm_null_delay(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_sframes_t *delayp)
{
	*delayp = 0;
	return 0;
}

static int snd_pcm_null_prepare(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	null->state = SND_PCM_STATE_PREPARED;
	null->appl_ptr = 0;
	null->hw_ptr = 0;
	return 0;
}

static int snd_pcm_null_reset(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	null->appl_ptr = 0;
	null->hw_ptr = 0;
	return 0;
}

static int snd_pcm_null_start(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	assert(null->state == SND_PCM_STATE_PREPARED);
	null->state = SND_PCM_STATE_RUNNING;
	if (pcm->stream == SND_PCM_STREAM_CAPTURE)
		snd_pcm_mmap_appl_forward(pcm, pcm->buffer_size);
	return 0;
}

static int snd_pcm_null_drop(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	assert(null->state != SND_PCM_STATE_OPEN);
	null->state = SND_PCM_STATE_SETUP;
	return 0;
}

static int snd_pcm_null_drain(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	assert(null->state != SND_PCM_STATE_OPEN);
	null->state = SND_PCM_STATE_SETUP;
	return 0;
}

static int snd_pcm_null_pause(snd_pcm_t *pcm, int enable)
{
	snd_pcm_null_t *null = pcm->private;
	if (enable) {
		if (null->state != SND_PCM_STATE_RUNNING)
			return -EBADFD;
	} else if (null->state != SND_PCM_STATE_PAUSED)
		return -EBADFD;
	null->state = SND_PCM_STATE_PAUSED;
	return 0;
}

static snd_pcm_sframes_t snd_pcm_null_rewind(snd_pcm_t *pcm, snd_pcm_uframes_t frames)
{
	snd_pcm_null_t *null = pcm->private;
	switch (null->state) {
	case SND_PCM_STATE_PREPARED:
	case SND_PCM_STATE_RUNNING:
		snd_pcm_mmap_appl_backward(pcm, frames);
		snd_pcm_mmap_hw_backward(pcm, frames);
		return frames;
	default:
		return -EBADFD;
	}
}

static snd_pcm_sframes_t snd_pcm_null_fwd(snd_pcm_t *pcm, snd_pcm_uframes_t size)
{
	snd_pcm_null_t *null = pcm->private;
	switch (null->state) {
	case SND_PCM_STATE_PREPARED:
	case SND_PCM_STATE_RUNNING:
		snd_pcm_mmap_appl_forward(pcm, size);
		snd_pcm_mmap_hw_forward(pcm, size);
		return size;
	default:
		return -EBADFD;
	}
}

static snd_pcm_sframes_t snd_pcm_null_writei(snd_pcm_t *pcm, const void *buffer ATTRIBUTE_UNUSED, snd_pcm_uframes_t size)
{
	snd_pcm_null_t *null = pcm->private;
	if (null->state == SND_PCM_STATE_PREPARED &&
	    pcm->start_mode != SND_PCM_START_EXPLICIT) {
		null->state = SND_PCM_STATE_RUNNING;
	}
	return snd_pcm_null_fwd(pcm, size);
}

static snd_pcm_sframes_t snd_pcm_null_writen(snd_pcm_t *pcm, void **bufs ATTRIBUTE_UNUSED, snd_pcm_uframes_t size)
{
	snd_pcm_null_t *null = pcm->private;
	if (null->state == SND_PCM_STATE_PREPARED &&
	    pcm->start_mode != SND_PCM_START_EXPLICIT) {
		null->state = SND_PCM_STATE_RUNNING;
	}
	return snd_pcm_null_fwd(pcm, size);
}

static snd_pcm_sframes_t snd_pcm_null_readi(snd_pcm_t *pcm, void *buffer ATTRIBUTE_UNUSED, snd_pcm_uframes_t size)
{
	snd_pcm_null_t *null = pcm->private;
	if (null->state == SND_PCM_STATE_PREPARED &&
	    pcm->start_mode != SND_PCM_START_EXPLICIT) {
		null->state = SND_PCM_STATE_RUNNING;
		snd_pcm_mmap_hw_forward(pcm, pcm->buffer_size);
	}
	return snd_pcm_null_fwd(pcm, size);
}

static snd_pcm_sframes_t snd_pcm_null_readn(snd_pcm_t *pcm, void **bufs ATTRIBUTE_UNUSED, snd_pcm_uframes_t size)
{
	snd_pcm_null_t *null = pcm->private;
	if (null->state == SND_PCM_STATE_PREPARED &&
	    pcm->start_mode != SND_PCM_START_EXPLICIT) {
		null->state = SND_PCM_STATE_RUNNING;
		snd_pcm_mmap_hw_forward(pcm, pcm->buffer_size);
	}
	return snd_pcm_null_fwd(pcm, size);
}

static snd_pcm_sframes_t snd_pcm_null_mmap_forward(snd_pcm_t *pcm, snd_pcm_uframes_t size)
{
	return snd_pcm_null_fwd(pcm, size);
}

static snd_pcm_sframes_t snd_pcm_null_avail_update(snd_pcm_t *pcm)
{
	return pcm->buffer_size;
}

static int snd_pcm_null_hw_refine(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params)
{
	int err = _snd_pcm_hw_refine(params);
	params->fifo_size = 0;
	return err;
}

static int snd_pcm_null_hw_params(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t * params ATTRIBUTE_UNUSED)
{
	return 0;
}

static int snd_pcm_null_sw_params(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_sw_params_t * params ATTRIBUTE_UNUSED)
{
	return 0;
}

static int snd_pcm_null_mmap(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	if (!(pcm->info & SND_PCM_INFO_MMAP)) {
		size_t size = snd_pcm_frames_to_bytes(pcm, pcm->buffer_size);
		int id = shmget(IPC_PRIVATE, size, 0666);
		if (id < 0) {
			SYSERR("shmget failed");
			return -errno;
		}
		null->shmid = id;
	}
	return 0;
}

static int snd_pcm_null_munmap(snd_pcm_t *pcm)
{
	snd_pcm_null_t *null = pcm->private;
	if (shmctl(null->shmid, IPC_RMID, 0) < 0) {
		SYSERR("shmctl IPC_RMID failed");
			return -errno;
	}
	return 0;
}

static void snd_pcm_null_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_output_printf(out, "Null PCM\n");
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
}

snd_pcm_ops_t snd_pcm_null_ops = {
	close: snd_pcm_null_close,
	card: snd_pcm_null_card,
	info: snd_pcm_null_info,
	hw_refine: snd_pcm_null_hw_refine,
	hw_params: snd_pcm_null_hw_params,
	sw_params: snd_pcm_null_sw_params,
	channel_info: snd_pcm_null_channel_info,
	dump: snd_pcm_null_dump,
	nonblock: snd_pcm_null_nonblock,
	async: snd_pcm_null_async,
	mmap: snd_pcm_null_mmap,
	munmap: snd_pcm_null_munmap,
};

snd_pcm_fast_ops_t snd_pcm_null_fast_ops = {
	status: snd_pcm_null_status,
	state: snd_pcm_null_state,
	delay: snd_pcm_null_delay,
	prepare: snd_pcm_null_prepare,
	reset: snd_pcm_null_reset,
	start: snd_pcm_null_start,
	drop: snd_pcm_null_drop,
	drain: snd_pcm_null_drain,
	pause: snd_pcm_null_pause,
	rewind: snd_pcm_null_rewind,
	writei: snd_pcm_null_writei,
	writen: snd_pcm_null_writen,
	readi: snd_pcm_null_readi,
	readn: snd_pcm_null_readn,
	avail_update: snd_pcm_null_avail_update,
	mmap_forward: snd_pcm_null_mmap_forward,
};

int snd_pcm_null_open(snd_pcm_t **pcmp, char *name, int stream, int mode)
{
	snd_pcm_t *pcm;
	snd_pcm_null_t *null;
	int fd;
	assert(pcmp);
	if (stream == SND_PCM_STREAM_PLAYBACK) {
		fd = open("/dev/null", O_WRONLY);
		if (fd < 0) {
			SYSERR("Cannot open /dev/null");
			return -errno;
		}
	} else {
		fd = open("/dev/full", O_RDONLY);
		if (fd < 0) {
			SYSERR("Cannot open /dev/full");
			return -errno;
		}
	}
	null = calloc(1, sizeof(snd_pcm_null_t));
	if (!null) {
		close(fd);
		return -ENOMEM;
	}
	null->poll_fd = fd;
	null->state = SND_PCM_STATE_OPEN;
	
	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		close(fd);
		free(null);
		return -ENOMEM;
	}
	if (name)
		pcm->name = strdup(name);
	pcm->type = SND_PCM_TYPE_NULL;
	pcm->stream = stream;
	pcm->mode = mode;
	pcm->ops = &snd_pcm_null_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = &snd_pcm_null_fast_ops;
	pcm->fast_op_arg = pcm;
	pcm->private = null;
	pcm->poll_fd = fd;
	pcm->hw_ptr = &null->hw_ptr;
	pcm->appl_ptr = &null->appl_ptr;
	*pcmp = pcm;

	return 0;
}

int _snd_pcm_null_open(snd_pcm_t **pcmp, char *name,
		       snd_config_t *conf, 
		       int stream, int mode)
{
	snd_config_iterator_t i;
	snd_config_foreach(i, conf) {
		snd_config_t *n = snd_config_entry(i);
		if (strcmp(n->id, "comment") == 0)
			continue;
		if (strcmp(n->id, "type") == 0)
			continue;
		if (strcmp(n->id, "stream") == 0)
			continue;
		ERR("Unknown field %s", n->id);
		return -EINVAL;
	}
	return snd_pcm_null_open(pcmp, name, stream, mode);
}
