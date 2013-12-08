/*
 * Copyright 2013 Dominic Spill
 * Copyright 2013 Adam Stasiak
 *
 * This file is part of USBProxy.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 *
 * Interface.cpp
 *
 * Created on: Nov 6, 2013
 */

#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include "HexString.h"

#include "Device.h"
#include "Interface.h"
#include "Endpoint.h"

#include "DeviceProxy.h"
#include "HID.h"
#include "USBString.h"

#include "Configuration.h"


//CLEANUP update active interface in interfacegroup upon set interface request
//CLEANUP update active endpoints in proxied device upon set interface request
//CLEANUP handle any endpoints that become inactive upon set interface request

Interface::Interface(Configuration* _configuration,__u8** p,const __u8* e) {
	configuration=_configuration;
	hid_descriptor=NULL;
	generic_descriptors=NULL;
	generic_descriptor_count=0;

	memcpy(&descriptor,*p,9);
	*p=*p+9;
	endpoints=(Endpoint**)calloc(descriptor.bNumEndpoints,sizeof(*endpoints));
	Endpoint** ep=endpoints;
	while (*p<e && (*(*p+1))!=4) {
		switch (*(*p+1)) {
			case 5:
				*(ep++)=new Endpoint(this,*p);
				break;
			case 0x21:
				hid_descriptor=new HID(*p);
				break;
			default:
				GenericDescriptor* d=(GenericDescriptor*)malloc((*p)[0]);
				memcpy(d,*p,(*p)[0]);
				generic_descriptor_count++;
				if (generic_descriptors) {
					generic_descriptors=(GenericDescriptor**)realloc(generic_descriptors,sizeof(*generic_descriptors)*generic_descriptor_count);
				} else {
					generic_descriptors=(GenericDescriptor**)malloc(sizeof(*generic_descriptors));
				}
				generic_descriptors[generic_descriptor_count-1]=d;
				break;
		}
		*p=*p+**p;
	}
}

Interface::Interface(Configuration* _configuration,const usb_interface_descriptor* _descriptor) {
	configuration=_configuration;
	hid_descriptor=NULL;
	generic_descriptors=NULL;
	generic_descriptor_count=0;

	descriptor=*_descriptor;
	endpoints=(Endpoint**)calloc(descriptor.bNumEndpoints,sizeof(*endpoints));
}

Interface::Interface(Configuration* _configuration,__u8 bInterfaceNumber,__u8 bAlternateSetting,__u8 bNumEndpoints,__u8 bInterfaceClass,__u8 bInterfaceSubClass,__u8 bInterfaceProtocol,__u8 iInterface) {
	configuration=_configuration;
	hid_descriptor=NULL;
	descriptor.bLength=9;
	descriptor.bDescriptorType=USB_DT_INTERFACE;
	descriptor.bInterfaceNumber=bInterfaceNumber;
	descriptor.bAlternateSetting=bAlternateSetting;
	descriptor.bNumEndpoints=bNumEndpoints;
	descriptor.bInterfaceClass=bInterfaceClass;
	descriptor.bInterfaceSubClass=bInterfaceSubClass;
	descriptor.bInterfaceProtocol=bInterfaceProtocol;
	descriptor.iInterface=iInterface;
	endpoints=(Endpoint**)calloc(descriptor.bNumEndpoints,sizeof(*endpoints));
	generic_descriptors=NULL;
	generic_descriptor_count=0;
}

Interface::~Interface() {
	int i;
	if (endpoints) {
		for(i=0;i<descriptor.bNumEndpoints;i++) {
			if (endpoints[i]) {
				delete(endpoints[i]);
				endpoints[i]=NULL;
			}
		}
		free(endpoints);
		endpoints=NULL;
	}
	if (hid_descriptor) {
		delete(hid_descriptor);
		hid_descriptor=NULL;
	}
	i=0;
	if (generic_descriptors) {
		for (i=0;i<generic_descriptor_count;i++) {
			free(generic_descriptors[i]);
			generic_descriptors[i]=NULL;
		}
		free(generic_descriptors);
		generic_descriptors=NULL;
	}
}

const usb_interface_descriptor* Interface::get_descriptor() {
	return &descriptor;
}

size_t Interface::get_full_descriptor_length() {
	size_t total=descriptor.bLength;
	if (hid_descriptor) {total+=hid_descriptor->get_full_descriptor_length();}
	int i=0;
	for (i=0;i<generic_descriptor_count;i++) {total+=generic_descriptors[i]->bLength;}
	for(i=0;i<descriptor.bNumEndpoints;i++) {total+=endpoints[i]->get_full_descriptor_length();}
	return total;
}

void Interface::get_full_descriptor(__u8** p) {
	memcpy(*p,&descriptor,descriptor.bLength);
	*p=*p+descriptor.bLength;
	if (hid_descriptor) {hid_descriptor->get_full_descriptor(p);}
	int i=0;
	for (i=0;i<generic_descriptor_count;i++) {
		memcpy(*p,generic_descriptors[i],generic_descriptors[i]->bLength);
		*p=*p+generic_descriptors[i]->bLength;
	}
	for(i=0;i<descriptor.bNumEndpoints;i++) {endpoints[i]->get_full_descriptor(p);}
}

void Interface::add_endpoint(Endpoint* endpoint) {
	int i;
	for(i=0;i<descriptor.bNumEndpoints;i++) {
		if (!endpoints[i]) {
			endpoints[i]=endpoint;
			break;
		} else {
			if (endpoints[i]->get_descriptor()->bEndpointAddress==endpoint->get_descriptor()->bEndpointAddress) {
				delete(endpoints[i]);
				/* not needed endpoints[i]=NULL; */
				endpoints[i]=endpoint;
				break;
			}
		}
	}
	fprintf(stderr,"Ran out of endpoint storage space on interface %d.",descriptor.bInterfaceNumber);
}

Endpoint* Interface::get_endpoint_by_idx(__u8 index) {
	if (index>=descriptor.bNumEndpoints || index<0) {return NULL;}
	return endpoints[index];
}

Endpoint* Interface::get_endpoint_by_address(__u8 address) {
	int i;
	for(i=0;i<descriptor.bNumEndpoints;i++) {
		if (endpoints[i]->get_descriptor()->bEndpointAddress==address) {return endpoints[i];}
	}
	return NULL;
}

__u8 Interface::get_endpoint_count() {
	return descriptor.bNumEndpoints;
}

void Interface::print(__u8 tabs,bool active) {
	unsigned int i;
	for(i=0;i<tabs;i++) {putchar('\t');}
	if (active) {putchar('*');}
	char* hex=hex_string(&descriptor,sizeof(descriptor));
	printf("Alt(%d): %s\n",descriptor.bAlternateSetting,hex);
	free(hex);
	if (descriptor.iInterface) {
		USBString* s=get_interface_string();
		if (s) {
			for(i=0;i<tabs;i++) {putchar('\t');}
			printf("  Name: ");
			s->print_ascii(stdout);
			putchar('\n');
		}
	}
	if (hid_descriptor) {hid_descriptor->print(tabs+1);}
	int j=0;
	for (j=0;j<generic_descriptor_count;j++) {
		for(i=0;i<(tabs+1);i++) {putchar('\t');}
		char* hex=hex_string((void*)generic_descriptors[j],generic_descriptors[j]->bLength);
		printf("Other(%02x): %s\n",generic_descriptors[j]->bDescriptorType,hex);
		free(hex);
	}
	for(i=0;i<descriptor.bNumEndpoints;i++) {
		if (endpoints[i]) {endpoints[i]->print(tabs+1);}
	}
}

USBString* Interface::get_interface_string(__u16 languageId) {
	if (!descriptor.iInterface) {return NULL;}
	return configuration->get_device()->get_string(descriptor.iInterface,languageId);
}

const GenericDescriptor* Interface::get_generic_descriptor(__u8 index) {
	if (index>=generic_descriptor_count || index<0) {return NULL;}
	return generic_descriptors[index];
}

__u8 Interface::get_generic_descriptor_count() {
	return generic_descriptor_count;
}

void Interface::add_generic_descriptor(GenericDescriptor* _gd) {
	GenericDescriptor* d=(GenericDescriptor*)malloc(_gd->bLength);
	memcpy(d,_gd,_gd->bLength);
	if (generic_descriptors) {
		generic_descriptors=(GenericDescriptor**)realloc(generic_descriptors,sizeof(*generic_descriptors)*generic_descriptor_count);
	} else {
		generic_descriptors=(GenericDescriptor**)malloc(sizeof(*generic_descriptors));
	}
	generic_descriptors[generic_descriptor_count-1]=d;
}

const definition_error Interface::is_defined(__u8 configId,__u8 interfaceNum) {
	if (descriptor.bLength!=9) {return definition_error(DE_ERR_INVALID_DESCRIPTOR,0x01, DE_OBJ_INTERFACE,configId,interfaceNum,descriptor.bAlternateSetting);}
	if (descriptor.bDescriptorType!=USB_DT_INTERFACE) {return definition_error(DE_ERR_INVALID_DESCRIPTOR,0x02, DE_OBJ_INTERFACE,configId,interfaceNum,descriptor.bAlternateSetting);}
	//__u8  bInterfaceNumber;
	//__u8  bAlternateSetting;
	//__u8  bNumEndpoints;
	//__u8  bInterfaceClass;
	//__u8  bInterfaceSubClass;
	//__u8  bInterfaceProtocol;
	//__u8  iInterface;

	int i;
	for (i=0;i<descriptor.bNumEndpoints;i++) {
		if (!endpoints[i]) {return definition_error(DE_ERR_NULL_OBJECT,0x0, DE_OBJ_ENDPOINT,configId,interfaceNum,descriptor.bAlternateSetting,i);}
		definition_error rc=endpoints[i]->is_defined(configId,interfaceNum,descriptor.bAlternateSetting);
		if (rc.error) {return rc;}
	}
	return definition_error();

}

Configuration* Interface::get_configuration() {return configuration;}