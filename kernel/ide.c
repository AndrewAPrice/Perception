#include "ide.h"
#include "liballoc.h"
#include "scheduler.h"
#include "storage_device.h"
#include "thread.h"
#include "text_terminal.h"
#include "virtual_allocator.h"

#define IDE_REQUEST_TYPE_READ 0

struct IDERequest {
	struct IDERequest *next;
	uint8 type;
	void *request; /* pointer to the parent object based on the type */
};

struct IDERequestRead {
	struct IDERequest request;

	struct IDEDevice *device;
	size_t offset;
	size_t length;
	size_t pml4;
	char *dest_buffer;
	StorageDeviceCallback callback;
	void *callback_tag;
};

/* Command/Status Port bitmask */
#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DSC     0x10
#define ATA_SR_DRQ     0x08
#define ATA_SR_CORR    0x04
#define ATA_SR_IDX     0x02
#define ATA_SR_ERR     0x01

/* Features/Error Port bit mask */
#define ATA_ER_BBK      0x80
#define ATA_ER_UNC      0x40
#define ATA_ER_MC       0x20
#define ATA_ER_IDNF     0x10
#define ATA_ER_MCR      0x08
#define ATA_ER_ABRT     0x04
#define ATA_ER_TK0NF    0x02
#define ATA_ER_AMNF     0x01

/* Command/Status port commands */
#define ATA_CMD_READ_PIO          0x20
#define ATA_CMD_READ_PIO_EXT      0x24
#define ATA_CMD_READ_DMA          0xC8
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_PIO         0x30
#define ATA_CMD_WRITE_PIO_EXT     0x34
#define ATA_CMD_WRITE_DMA         0xCA
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_CACHE_FLUSH       0xE7
#define ATA_CMD_CACHE_FLUSH_EXT   0xEA
#define ATA_CMD_PACKET            0xA0
#define ATA_CMD_IDENTIFY_PACKET   0xA1
#define ATA_CMD_IDENTIFY          0xEC

/* ATAPI commands */
#define      ATAPI_CMD_READ       0xA8
#define      ATAPI_CMD_EJECT      0x1B

/* offset for the ATAPI identifiers */
#define ATA_IDENT_DEVICETYPE   0
#define ATA_IDENT_CYLINDERS    2
#define ATA_IDENT_HEADS        6
#define ATA_IDENT_SECTORS      12
#define ATA_IDENT_SERIAL       20
#define ATA_IDENT_MODEL        54
#define ATA_IDENT_CAPABILITIES 98
#define ATA_IDENT_FIELDVALID   106
#define ATA_IDENT_MAX_LBA      120
#define ATA_IDENT_COMMANDSETS  164
#define ATA_IDENT_MAX_LBA_EXT  200

/* for selecting a drive */
#define IDE_ATA        0x00
#define IDE_ATAPI      0x01
 
#define ATA_MASTER     0x00
#define ATA_SLAVE      0x01

/* ALTSTATUS/CONTROL port */
#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07
#define ATA_REG_SECCOUNT1  0x08
#define ATA_REG_LBA3       0x09
#define ATA_REG_LBA4       0x0A
#define ATA_REG_LBA5       0x0B
#define ATA_REG_CONTROL    0x0C
#define ATA_REG_ALTSTATUS  0x0C
#define ATA_REG_DEVADDRESS 0x0D

/* Channels: */
#define      ATA_PRIMARY      0x00
#define      ATA_SECONDARY    0x01
 
/* Directions: */
#define      ATA_READ      0x00
#define      ATA_WRITE     0x01

/* The default and seemingly universal sector size for CD-ROMs. */
#define ATAPI_SECTOR_SIZE 2048
 
/* The default ISA IRQ numbers of the ATA controllers. */
#define ATA_IRQ_PRIMARY     0x0E
#define ATA_IRQ_SECONDARY   0x0F
 
/* The necessary I/O ports, indexed by "bus". */
#define ATA_DATA(x)         (x)
#define ATA_FEATURES(x)     (x+1)
#define ATA_SECTOR_COUNT(x) (x+2)
#define ATA_ADDRESS1(x)     (x+3)
#define ATA_ADDRESS2(x)     (x+4)
#define ATA_ADDRESS3(x)     (x+5)
#define ATA_DRIVE_SELECT(x) (x+6)
#define ATA_COMMAND(x)      (x+7)
#define ATA_DCR(x)          (x+0x206)   /* device control register */
 
/* valid values for "bus/channel" */
#define ATA_BUS_PRIMARY     0x1F0
#define ATA_BUS_SECONDARY   0x170

/* valid values for "drive" */
#define ATA_DRIVE_MASTER    0xA0
#define ATA_DRIVE_SLAVE     0xB0
 
/* ATA specifies a 400ns delay after drive switching -- often
 * implemented as 4 Alternative Status queries. */
#define ATA_SELECT_DELAY(bus) \
  {inportb(ATA_DCR(bus));inportb(ATA_DCR(bus));inportb(ATA_DCR(bus));inportb(ATA_DCR(bus));}

/* command set for ATAPI packets can be found here:
http://wiki.osdev.org/ATAPI#Complete_Command_Set
http://www.t10.org/ftp/x3t9.2/document.87/87-106r0.txt
*/


void ide_write(struct IDEChannelRegisters *channel, uint8 reg, uint8 data) {
	if(reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, 0x80 | channel->no_interrupt);
	if(reg < 0x08)
		outportb(channel->io_base + reg - 0x00, data);
	else if (reg < 0x0C)
		outportb(channel->io_base + reg - 0x06, data);
	else if(reg < 0x0E)
		outportb(channel->control_base + reg - 0x0A, data);
	else if(reg < 0x16)
		outportb(channel->bus_master_id + reg - 0x0E, data);
	if(reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, channel->no_interrupt);
}

uint8 ide_read(struct IDEChannelRegisters *channel, uint8 reg) {
	uint8 out;

	if(reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, 0x80 | channel->no_interrupt);
	if(reg < 0x08)
		out = inportb(channel->io_base + reg - 0x00);
	else if (reg < 0x0C)
		out = inportb(channel->io_base + reg - 0x06);
	else if(reg < 0x0E)
		out = inportb(channel->control_base + reg - 0x0A);
	else if(reg < 0x16)
		out = inportb(channel->bus_master_id + reg - 0x0E);
	else
		out = 0;

	if(reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, channel->no_interrupt);

	return out;
}

void ide_read_buffer(struct IDEChannelRegisters *channel, uint8 reg, void *buffer, size_t quads) {
	if(reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, 0x80 | channel->no_interrupt);

	/* ES and ESP is trashed between the inline assembly blocks */
	// asm("pushw %es; movw %ds, %ax; movw %ax, %es");

	uint16 port;

	if(reg < 0x08)
		port = channel->io_base + reg - 0x00;
	else if(reg < 0x0C)
		port = channel->io_base + reg - 0x06;
	else if(reg < 0x0E)
		port = channel->control_base + reg - 0x0A;
	else if(reg < 0x16)
		port = channel->bus_master_id + reg - 0x0E;
	else
		port = 0;

	if(port != 0) {
		uint32 *ptr = buffer;
		size_t i; for(i = 0; i < quads; i++, ptr++)
			*ptr = inportdw(port);
	}
	
	// asm("popw %es");
	if(reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, channel->no_interrupt);
}

uint8 ide_polling(struct IDEChannelRegisters *channel, uint32 advanced_check) {
	size_t i; for(i = 0; i < 4; i++)
		ide_read(channel, ATA_REG_ALTSTATUS); /* reading the alt status port wasts 100ns */

	while(ide_read(channel, ATA_REG_STATUS) & ATA_SR_BSY) ; /* wait for BSY to be zero */

	if(advanced_check) {
		uint8 state = ide_read(channel, ATA_REG_STATUS); /* read status register */
		if(state & ATA_SR_ERR) return 2; /* error */
		if(state & ATA_SR_DF) return 1; /* device fault */
		if((state & ATA_SR_DRQ) == 0) return 3; /* DRQ should be set */
	}

	return 0;
}

void ide_thread_entry(struct IDEController *controller);
void ide_read_handler (void *storage_device_tag, size_t offset, size_t length, size_t pml4, char *dest_buffer,
	StorageDeviceCallback callback, void *callback_tag);

void init_ide(struct PCIDevice *device) {
	char *buffer = malloc(2048);
	if(buffer == 0) return;

	struct IDEController *controller = malloc(sizeof(struct IDEController));
	if(controller == 0) { free(buffer); return; } /* no memory */

	device->driver = 1;
	controller->Devices = 0;

	/* read in the ports */
	uint16 bar0 = pci_config_read_word(device->bus, device->slot, device->function, PCI_HDR_BAR0);
	uint16 bar1 = pci_config_read_word(device->bus, device->slot, device->function, PCI_HDR_BAR1);
	uint16 bar2 = pci_config_read_word(device->bus, device->slot, device->function, PCI_HDR_BAR2);
	uint16 bar3 = pci_config_read_word(device->bus, device->slot, device->function, PCI_HDR_BAR3);
	uint16 bar4 = pci_config_read_word(device->bus, device->slot, device->function, PCI_HDR_BAR4);

	uint8 prog_if = pci_config_read_byte(device->bus, device->slot, device->function, PCI_HDR_PROG_IF);
#if 0
	if(prog_if == 0x8A || prog_if == 0x80) {
		print_string("IDE "); /* irq 14 and 15 */
	} else {
		print_string("SATA");
	}
#endif

	controller->Channels[ATA_PRIMARY].io_base = (bar0 & 0xFFFC) + 0x1F0 * (!bar0);
	controller->Channels[ATA_PRIMARY].control_base = (bar1 & 0xFFFC) + 0x3F6 * (!bar1);
	controller->Channels[ATA_SECONDARY].io_base = (bar2 & 0xFFFC) + 0x170 * (!bar2);
	controller->Channels[ATA_SECONDARY].control_base = (bar3 & 0xFFFC) + 0x736 * (!bar3);
	controller->Channels[ATA_PRIMARY].bus_master_id = (bar4 & 0xFFFC);
	controller->Channels[ATA_SECONDARY].bus_master_id = (bar4 & 0xFFC) + 8;

	/* disable irqs */
	ide_write(&controller->Channels[ATA_PRIMARY], ATA_REG_CONTROL, 2);
	ide_write(&controller->Channels[ATA_SECONDARY], ATA_REG_CONTROL, 2);

	/* detect ATA-ATAPI devices */
	size_t i = 0; for(i = 0; i < 2; i++) {
		size_t j = 0; for(j = 0; j < 2; j++) {
			uint8 err = 0;
			uint8 type = IDE_ATA;

			/* select drive */
			ide_write(&controller->Channels[i], ATA_REG_HDDEVSEL, 0xA0 | (j << 4));
			asm("pause");//sleep(1); /* wait 1 ms */

			/* send the identify command */
			ide_write(&controller->Channels[i], ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
			asm("pause");//sleep(1);

			/* poll*/
			if(ide_read(&controller->Channels[i], ATA_REG_STATUS) == 0)
				continue; /* no device */

			while(1) {
				uint8 status = ide_read(&controller->Channels[i], ATA_REG_STATUS);
				if((status & ATA_SR_ERR)) {err = 1; break;} /* not ATA */
				if(!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
			}

			/* prove for ATAPI device */
			if(err != 0) {
				uint8 cl = ide_read(&controller->Channels[i], ATA_REG_LBA1);
				uint8 ch = ide_read(&controller->Channels[i], ATA_REG_LBA2);

				if(cl == 0x14 && ch == 0xEB)
					type = IDE_ATAPI;
				else if(cl == 0x69 && ch == 0x96)
					type = IDE_ATAPI;
				else
					continue; /* unknown type */

				ide_write(&controller->Channels[i], ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
				asm("pause");//sleep(1);
			}

			/* allocate our device object */
			struct IDEDevice *dev = malloc(sizeof(struct IDEDevice));
			if(dev == 0) {
				/* out of memory */
				while(controller->Devices) {
					struct IDEDevice *next = controller->Devices->Next;
					free(controller->Devices);
					controller->Devices = next;
				}

				free(controller);
				free(buffer);
				device->driver = 0;
				return;
			}

			dev->Next = controller->Devices;
			controller->Devices = dev;


			/* read identification space of the device */
			
			ide_read_buffer(&controller->Channels[i], ATA_REG_DATA, (void *)buffer, 128);

			/* read device parameters */
			dev->Reserved = 0;
			dev->Type = type;
			dev->Channel = i;
			dev->Drive = j;
			dev->Signature = *(uint16 *)&buffer[ATA_IDENT_DEVICETYPE];
			dev->Capabilities = *(uint16 *)&buffer[ATA_IDENT_CAPABILITIES];
			dev->CommandSets = *(uint32 *)&buffer[ATA_IDENT_COMMANDSETS];
			dev->Controller = controller;

			/* get the size */
			if(dev->CommandSets & (1 << 26))
				dev->Size = *(uint32 *)&buffer[ATA_IDENT_MAX_LBA_EXT]; /* 48-bit addressing */
			else
				dev->Size = *(uint32 *)&buffer[ATA_IDENT_MAX_LBA]; /* 28-bit addressing or CHS */

			/* indicates the device model */
			size_t k; for(k = 0; k < 40; k += 2) {
				dev->Model[k] = buffer[ATA_IDENT_MODEL + k + 1];
				dev->Model[k + 1] = buffer[ATA_IDENT_MODEL + k];
			}
			dev->Model[40] = 0;
		}
	}

	free(buffer);

	/* finished initializing */

	if(controller->Devices == 0) {
		free(controller); /* we didn't find any devices */
		return;
	}

	/* create a thread for controlling this device */
	struct Thread *thread = create_thread(0, (size_t)ide_thread_entry, (size_t)controller);
	if(!thread) {
		while(controller->Devices) {
			struct IDEDevice *next = controller->Devices->Next;
			free(controller->Devices);
			controller->Devices = next;
		}
		free(controller);
		device->driver = 0; /* out of memory */
		return;
	}
	controller->thread = thread;

	/* loop over each device and register it */
	struct IDEDevice *dev = controller->Devices;
	while(dev != 0) {
		struct StorageDevice *sd = malloc(sizeof(struct StorageDevice));
		if(!sd) return; /* out of memory */
		dev->storage_device = sd;

		if(dev->Type == IDE_ATAPI) {
			sd->type = STORAGE_DEVICE_TYPE_OPTICAL;
			sd->inserted = 0;
			sd->size = 0; /* atapi sector sizes are 2048 bytes */
		} else {
			sd->type = STORAGE_DEVICE_TYPE_HARDDRIVE;
			sd->inserted = 1;
			sd->size = dev->Size * 512; /* ide ector sizes are 512 bytes */
		}

		sd->tag = dev;
		sd->read_handler = ide_read_handler;
		dev = dev->Next;
	}

	/* clear the queue of requests */
	controller->FirstRequest = 0;
	controller->LastRequest = 0;

	schedule_thread(thread); /* schedule the thread to run, because we can do things like detect optical drives */
}

/* thread for controlling an ide device */
void ide_thread_entry(struct IDEController *controller) {
	/* detect any inserted media and register our drives */
	struct IDEDevice *dev = controller->Devices;
	while(dev != 0) {
		if(dev->Type == IDE_ATAPI) {
			/* got us a cd drive! */

			/* select drive - master/slave */
			uint16 bus = dev->Channel ? ATA_BUS_SECONDARY : ATA_BUS_PRIMARY;
			outportb(ATA_DRIVE_SELECT(bus), dev->Drive << 4);
			/* wait 400ns */
			ATA_SELECT_DELAY(bus);

			/* set features register to 0 (PIO Mode) */
			outportb(ATA_FEATURES(bus), 0x0);

			/* set lba1 and lba2 registers to 0x0008 (number of bytes will be returned ) */
			outportb(ATA_ADDRESS2(bus), 8);//ATAPI_SECTOR_SIZE & 0xFF);
			outportb(ATA_ADDRESS3(bus), 0);//ATAPI_SECTOR_SIZE >> 8);

			/* send packet command */
			outportb(ATA_COMMAND(bus), 0xA0);

			/* poll */
			uint8 status;
			while((status = inportb(ATA_COMMAND(bus))) & 0x80) /* busy */
				asm volatile ("hlt");


			while(!((status = inportb(ATA_COMMAND(bus))) & 0x8) && !(status & 0x1))
				asm volatile ("hlt");

			/* is there an error ? */
			if(status & 0x1) {
				/* no disk */
				goto out_of_loop;
			}

			/* send the atapi packet - must be 6 words (12 bytes) long */
			uint8 atapi_packet[12] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
			for(status = 0; status < 12; status += 2) {
				outportw(ATA_DATA(bus),*(uint16 *)&atapi_packet[status]);
			}

			/* todo - wait for an irq */
			for(status = 0; status < 15; status++) asm volatile ("hlt");

			/* read 4 words (8 bytes) from the data register */
			uint32 returnLba = 
				(inportw(ATA_DATA(bus)) << 0) |
				(inportw(ATA_DATA(bus)) << 16);
			
			uint32 blockLengthInBytes =
				(inportw(ATA_DATA(bus)) << 0) |
				(inportw(ATA_DATA(bus)) << 16);
			
			/* flip the endiness */
			returnLba = (((returnLba >> 0) & 0xFF) << 24) |
				(((returnLba >> 8) & 0xFF) << 16) |
				(((returnLba >> 16) & 0xFF) << 8) |
				(((returnLba >> 24) & 0xFF) << 0);

			blockLengthInBytes = (((blockLengthInBytes >> 0) & 0xFF) << 24) |
				(((blockLengthInBytes >> 8) & 0xFF) << 16) |
				(((blockLengthInBytes >> 16) & 0xFF) << 8) |
				(((blockLengthInBytes >> 24) & 0xFF) << 0);

			/* calculate the device size */
			dev->storage_device->size = returnLba * blockLengthInBytes;
			dev->storage_device->inserted = 1;
		}
	out_of_loop:
		/* add this device, even if it's not inserted */
		add_storage_device(dev->storage_device);

		dev = dev->Next;
	}


	/* enter the event loop */
	while(sleep_if_not_set((size_t *)&controller->FirstRequest)) {

		/* grab the top value */
		lock_interrupts();

		if(!controller->FirstRequest) {
			/* something else woke us up */
			unlock_interrupts();
			continue;
		}

		/* take off the front element from the queue */
		struct IDERequest *request = controller->FirstRequest;
		if(request == controller->LastRequest) {
			/* clear the queue */
			controller->FirstRequest = 0;
			controller->LastRequest = 0;
		} else
			controller->FirstRequest = request->next;

		unlock_interrupts();

		/* do something with the request */

		switch(request->type) {
		case IDE_REQUEST_TYPE_READ: {
			struct IDERequestRead *request_read = (struct IDERequestRead *)request->request;

			if(request_read->device->Type == IDE_ATAPI) {
				/* select drive - master/slave */
				uint16 bus = request_read->device->Channel ? ATA_BUS_SECONDARY : ATA_BUS_PRIMARY;
				outportb(ATA_DRIVE_SELECT(bus), request_read->device->Drive << 4);
				/* wait 400ns */
				ATA_SELECT_DELAY(bus);

				/* set features register to 0 (PIO Mode) */
				outportb(ATA_FEATURES(bus), 0x0);

				/* set lba1 and lba2 registers to 0x0008 (number of bytes will be returned ) */
				outportb(ATA_ADDRESS2(bus), ATAPI_SECTOR_SIZE & 0xFF);
				outportb(ATA_ADDRESS3(bus), ATAPI_SECTOR_SIZE >> 8);
				

				size_t start_lba = request_read->offset / ATAPI_SECTOR_SIZE;
				size_t end_lba = (request_read->offset + request_read->length + ATAPI_SECTOR_SIZE - 1) / ATAPI_SECTOR_SIZE;
				size_t lba; for (lba = start_lba; lba <= end_lba; lba++) {
					/* send packet command */
					outportb(ATA_COMMAND(bus), 0xA0);

					/* poll */
					uint8 status;
					while((status = inportb(ATA_COMMAND(bus))) & 0x80) /* busy */
						asm volatile ("hlt");


					while(!((status = inportb(ATA_COMMAND(bus))) & 0x8) && !(status & 0x1))
						asm volatile ("hlt");


					/* is there an error ? */
					if(status & 0x1) {
						/* no disk */
						request_read->callback(STORAGE_DEVICE_CALLBACK_STATUS_ERROR, request_read->callback_tag);
					} else {
						/* send the atapi packet - must be 6 words (12 bytes) long */
						uint8 atapi_packet[12] = {0xA8, 0,
							(lba >> 0x18) & 0xFF,
							(lba >> 0x10) & 0xFF,
							(lba >> 0x08) & 0xFF,
							(lba >> 0x00) & 0xFF, 0, 0, 0, 1, 0, 0};

						for(status = 0; status < 12; status += 2) {
							outportw(ATA_DATA(bus),*(uint16 *)&atapi_packet[status]);
						}

						/* todo - wait for an irq */
						for(status = 0; status < 15; status++) asm volatile ("hlt");

						/* switch to this address space */
						if(request_read->pml4 != kernel_pml4)
							switch_to_address_space(request_read->pml4);


						/* read in the data */
						size_t indx = lba * ATAPI_SECTOR_SIZE;
						size_t i = 0; for(i = 0; i < ATAPI_SECTOR_SIZE; i+=2, indx+=2) {
							uint16 b = inportw(ATA_DATA(bus));

							if(indx >= request_read->offset && indx < request_read->offset + request_read->length) {
								/* copy two bytes */
								*(uint16 *)&request_read->dest_buffer[indx - request_read->offset] = b;
							} else if(indx == request_read->offset + request_read->length - 1)
								/* copy just the last byte */
								request_read->dest_buffer[indx - request_read->offset] = (b > 8) & 0xFF;
						}
					}
				}

				/* call the callback */
				request_read->callback(STORAGE_DEVICE_CALLBACK_STATUS_SUCCESS, request_read->callback_tag);

			} else {
				/* not implemented */
				request_read->callback(STORAGE_DEVICE_CALLBACK_STATUS_ERROR, request_read->callback_tag);
			}
			break; }
		}

		free(request->request);
	}
}

void ide_read_handler (void *storage_device_tag, size_t offset, size_t length, size_t pml4, char *dest_buffer,
	StorageDeviceCallback callback, void *callback_tag) {

	struct IDERequestRead *request = malloc(sizeof(struct IDERequestRead));
	if(!request) {
		/* couldn't allocate room for the request - call the callback */
		callback(STORAGE_DEVICE_CALLBACK_STATUS_ERROR, callback_tag);
		return;
	}

	request->request.next = 0;
	request->request.type = IDE_REQUEST_TYPE_READ;
	request->request.request = request;
	request->device = (struct IDEDevice *)storage_device_tag;
	request->offset = offset;
	request->length = length;
	request->pml4 = pml4;
	request->dest_buffer = dest_buffer;
	request->callback = callback;
	request->callback_tag = callback_tag;

	/* queue this message */
	struct IDEController *controller = request->device->Controller;
	lock_interrupts();
	if(!controller->LastRequest) {
		/* only request */
		controller->FirstRequest = (struct IDERequest *)request;
		controller->LastRequest = (struct IDERequest *)request;
	} else {
		controller->LastRequest->next = (struct IDERequest *)request;
		controller->LastRequest = (struct IDERequest *)request;
	}

	/* wake up the ide thread */
	schedule_thread(request->device->Controller->thread);

	unlock_interrupts();
}