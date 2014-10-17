#include "ide.h"
#include "liballoc.h"

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

void init_ide(struct PCIDevice *device) {
	char *buffer = (char *)malloc(2048);
	if(buffer == 0) return;

	struct IDEController *controller = (struct IDEController*)malloc(sizeof(struct IDEController));
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
	if(prog_if == 0x8A || prog_if == 0x80) {
		print_string("IDE "); /* irq 14 and 15 */
	} else {
		print_string("SATA");
	}

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
			struct IDEDevice *dev = (struct IDEDevice *)malloc(sizeof(struct IDEDevice));
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

}
