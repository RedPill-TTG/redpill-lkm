/**
 * Cherry-picked USB.h internal structures from Linux v4.4.x. If possible avoid using anything from this file like fire.
 *
 * ORIGINAL FILE HEADER PRESERVED BELOW
 * ------------------------------------
 * Driver for USB Mass Storage compliant devices
 * Main Header File
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef REDPILL_USB_H
#define REDPILL_USB_H

#warning "Using compatibility file for drivers/usb/storage/usb.h - if possible do NOT compile using toolkit"

//This structure didn't change substantially since v2.6 days; 5.14 is simply the newest one we checked - it will
// probably remain unchanged for years to come
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0) && LINUX_VERSION_CODE < KERNEL_VERSION(5,14,0) //v3.3 - v5.14
#include <linux/usb.h> //struct usb_sg_request

struct us_data;
typedef int (*trans_cmnd)(struct scsi_cmnd *, struct us_data*);
typedef int (*trans_reset)(struct us_data*);
typedef void (*proto_cmnd)(struct scsi_cmnd*, struct us_data*);
typedef void (*extra_data_destructor)(void *);	/* extra data destructor */
typedef void (*pm_hook)(struct us_data *, int);	/* power management hook */

struct us_data {
	/* The device we're working with
	 * It's important to note:
	 *    (o) you must hold dev_mutex to change pusb_dev
	 */
	struct mutex		dev_mutex;	 /* protect pusb_dev */
	struct usb_device	*pusb_dev;	 /* this usb_device */
	struct usb_interface	*pusb_intf;	 /* this interface */
	struct us_unusual_dev   *unusual_dev;	 /* device-filter entry     */
	unsigned long		fflags;		 /* fixed flags from filter */
	unsigned long		dflags;		 /* dynamic atomic bitflags */
	unsigned int		send_bulk_pipe;	 /* cached pipe values */
	unsigned int		recv_bulk_pipe;
	unsigned int		send_ctrl_pipe;
	unsigned int		recv_ctrl_pipe;
	unsigned int		recv_intr_pipe;

	/* information about the device */
	char			*transport_name;
	char			*protocol_name;
	__le32			bcs_signature;
	u8			subclass;
	u8			protocol;
	u8			max_lun;

	u8			ifnum;		 /* interface number   */
	u8			ep_bInterval;	 /* interrupt interval */

	/* function pointers for this device */
	trans_cmnd		transport;	 /* transport function	   */
	trans_reset		transport_reset; /* transport device reset */
	proto_cmnd		proto_handler;	 /* protocol handler	   */

	/* SCSI interfaces */
	struct scsi_cmnd	*srb;		 /* current srb		*/
	unsigned int		tag;		 /* current dCBWTag	*/
	char			scsi_name[32];	 /* scsi_host name	*/

	/* control and bulk communications data */
	struct urb		*current_urb;	 /* USB requests	 */
	struct usb_ctrlrequest	*cr;		 /* control requests	 */
	struct usb_sg_request	current_sg;	 /* scatter-gather req.  */
	unsigned char		*iobuf;		 /* I/O buffer		 */
	dma_addr_t		iobuf_dma;	 /* buffer DMA addresses */
	struct task_struct	*ctl_thread;	 /* the control thread   */

	/* mutual exclusion and synchronization structures */
	struct completion	cmnd_ready;	 /* to sleep thread on	    */
	struct completion	notify;		 /* thread begin/end	    */
	wait_queue_head_t	delay_wait;	 /* wait during reset	    */
	struct delayed_work	scan_dwork;	 /* for async scanning      */

	/* subdriver information */
	void			*extra;		 /* Any extra data          */
	extra_data_destructor	extra_destructor;/* extra data destructor   */
#ifdef CONFIG_PM
	pm_hook			suspend_resume_hook;
#endif

	/* hacks for READ CAPACITY bug handling */
	int			use_last_sector_hacks;
	int			last_sector_retries;
};
#endif //LINUX_VERSION_CODE check


struct Scsi_Host;
static inline struct us_data *host_to_us(struct Scsi_Host *host) {
    return (struct us_data *) host->hostdata;
}

#endif //REDPILL_USB_H