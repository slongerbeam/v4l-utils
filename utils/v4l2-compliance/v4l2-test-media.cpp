/*
    V4L2 API subdev ioctl tests.

    Copyright (C) 2018  Hans Verkuil <hans.verkuil@cisco.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335  USA
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <set>
#include <fstream>

#include "v4l2-compliance.h"

int testMediaDeviceInfo(struct node *node)
{
	struct media_device_info mdinfo;

	memset(&mdinfo, 0xff, sizeof(mdinfo));
	fail_on_test(doioctl(node, MEDIA_IOC_DEVICE_INFO, &mdinfo));
	fail_on_test(check_0(mdinfo.reserved, sizeof(mdinfo.reserved)));
	fail_on_test(check_string(mdinfo.driver, sizeof(mdinfo.driver)));
	fail_on_test(mdinfo.model[0] && check_string(mdinfo.model, sizeof(mdinfo.model)));
	fail_on_test(mdinfo.serial[0] && check_string(mdinfo.serial, sizeof(mdinfo.serial)));
	fail_on_test(mdinfo.bus_info[0] && check_string(mdinfo.bus_info, sizeof(mdinfo.bus_info)));
	fail_on_test(mdinfo.media_version == 0);
	fail_on_test(mdinfo.driver_version != mdinfo.media_version);
	return 0;
}

static int checkDevice(__u32 major, __u32 minor, bool iface, __u32 id)
{
	char dev_path[100];
	fail_on_test(snprintf(dev_path, sizeof(dev_path), "/sys/dev/char/%d:%d",
			      major, minor) == -1);
	DIR *dp = opendir(dev_path);
	if (dp == NULL)	
		return fail("couldn't find %s for %s %u\n",
			    dev_path, iface ? "interface" : "entity", id);
	closedir(dp);
	return 0;
}

typedef std::set<__u32> id_set;

static media_v2_topology topology;
static media_v2_entity *v2_ents;
static id_set v2_entities_set;
static media_v2_interface *v2_ifaces;
static id_set v2_interfaces_set;
static media_v2_pad *v2_pads;
static id_set v2_pads_set;
static media_v2_link *v2_links;
static id_set v2_links_set;
static std::map<__u32, __u32> entity_num_pads;
static std::map<__u32, media_v2_entity *> v2_entity_map;
static unsigned num_data_links;

int testMediaTopology(struct node *node)
{
	memset(&topology, 0xff, sizeof(topology));
	topology.ptr_entities = 0;
	topology.ptr_interfaces = 0;
	topology.ptr_pads = 0;
	topology.ptr_links = 0;
	fail_on_test(doioctl(node, MEDIA_IOC_G_TOPOLOGY, &topology));
	fail_on_test(!topology.num_entities);
	fail_on_test(!topology.num_pads);
	fail_on_test(topology.topology_version == ~0ULL);
	fail_on_test(topology.num_entities == ~0U);
	fail_on_test(topology.num_interfaces == ~0U);
	fail_on_test(topology.num_pads == ~0U);
	fail_on_test(topology.num_links == ~0U);
	//fail_on_test(topology.reserved1);
	//fail_on_test(topology.reserved2);
	//fail_on_test(topology.reserved3);
	//fail_on_test(topology.reserved4);
	topology.ptr_entities = 4;
	fail_on_test(doioctl(node, MEDIA_IOC_G_TOPOLOGY, &topology) != EFAULT);
	topology.ptr_entities = 0;
	topology.ptr_interfaces = 4;
	fail_on_test(topology.num_interfaces &&
		     doioctl(node, MEDIA_IOC_G_TOPOLOGY, &topology) != EFAULT);
	topology.ptr_interfaces = 0;
	topology.ptr_pads = 4;
	fail_on_test(doioctl(node, MEDIA_IOC_G_TOPOLOGY, &topology) != EFAULT);
	topology.ptr_pads = 0;
	topology.ptr_links = 4;
	fail_on_test(topology.num_links &&
		     doioctl(node, MEDIA_IOC_G_TOPOLOGY, &topology) != EFAULT);
	topology.ptr_links = 0;
	v2_ents = new media_v2_entity[topology.num_entities];
	memset(v2_ents, 0xff, topology.num_entities * sizeof(*v2_ents));
	topology.ptr_entities = (__u64)v2_ents;
	v2_ifaces = new media_v2_interface[topology.num_interfaces];
	memset(v2_ifaces, 0xff, topology.num_interfaces * sizeof(*v2_ifaces));
	topology.ptr_interfaces = (__u64)v2_ifaces;
	v2_pads = new media_v2_pad[topology.num_pads];
	memset(v2_pads, 0xff, topology.num_pads * sizeof(*v2_pads));
	topology.ptr_pads = (__u64)v2_pads;
	v2_links = new media_v2_link[topology.num_links];
	memset(v2_links, 0xff, topology.num_links * sizeof(*v2_links));
	topology.ptr_links = (__u64)v2_links;
	fail_on_test(doioctl(node, MEDIA_IOC_G_TOPOLOGY, &topology));
	fail_on_test(v2_ents != (media_v2_entity *)topology.ptr_entities);
	fail_on_test(v2_ifaces != (media_v2_interface *)topology.ptr_interfaces);
	fail_on_test(v2_pads != (media_v2_pad *)topology.ptr_pads);
	fail_on_test(v2_links != (media_v2_link *)topology.ptr_links);

	for (unsigned i = 0; i < topology.num_entities; i++) {
		media_v2_entity &ent = v2_ents[i];

		fail_on_test(check_0(ent.reserved, sizeof(ent.reserved)));
		fail_on_test(check_string(ent.name, sizeof(ent.name)));
		fail_on_test(!ent.id);
		fail_on_test(ent.function == MEDIA_ENT_F_UNKNOWN);
		fail_on_test(ent.function == MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN);
		fail_on_test(ent.function == MEDIA_ENT_T_DEVNODE_UNKNOWN);
		fail_on_test(v2_entities_set.find(ent.id) != v2_entities_set.end());
		v2_entities_set.insert(ent.id);
		v2_entity_map[ent.id] = &ent;
	}
	for (unsigned i = 0; i < topology.num_interfaces; i++) {
		media_v2_interface &iface = v2_ifaces[i];

		fail_on_test(check_0(iface.reserved, sizeof(iface.reserved)));
		fail_on_test(checkDevice(iface.devnode.major, iface.devnode.minor,
					 true, iface.id));
		fail_on_test(!iface.id);
		fail_on_test(!iface.intf_type);
		fail_on_test(iface.intf_type < MEDIA_INTF_T_DVB_BASE);
		fail_on_test(iface.intf_type > MEDIA_INTF_T_V4L_BASE + 0xff);
		fail_on_test(iface.flags);
		fail_on_test(v2_interfaces_set.find(iface.id) != v2_interfaces_set.end());
		v2_interfaces_set.insert(iface.id);
	}
	for (unsigned i = 0; i < topology.num_pads; i++) {
		media_v2_pad &pad = v2_pads[i];
		__u32 fl = pad.flags;

		fail_on_test(check_0(pad.reserved, sizeof(pad.reserved)));
		fail_on_test(!pad.id);
		fail_on_test(!pad.entity_id);
		fail_on_test(v2_pads_set.find(pad.id) != v2_pads_set.end());
		v2_pads_set.insert(pad.id);
		fail_on_test(v2_entities_set.find(pad.entity_id) == v2_entities_set.end());
		fail_on_test(!(fl & (MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_SOURCE)));
		fail_on_test((fl & (MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_SOURCE)) ==
			     (MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_SOURCE));
		entity_num_pads[pad.entity_id]++;
	}
	for (unsigned i = 0; i < topology.num_links; i++) {
		media_v2_link &link = v2_links[i];
		bool is_iface = link.flags & MEDIA_LNK_FL_LINK_TYPE;

		fail_on_test(check_0(link.reserved, sizeof(link.reserved)));
		fail_on_test(!link.id);
		fail_on_test(!link.source_id);
		fail_on_test(!link.sink_id);
		fail_on_test(v2_links_set.find(link.id) != v2_links_set.end());
		v2_links_set.insert(link.id);
		if (is_iface) {
			fail_on_test(v2_interfaces_set.find(link.source_id) == v2_interfaces_set.end());
			fail_on_test(v2_entities_set.find(link.sink_id) == v2_entities_set.end());
		} else {
			fail_on_test(v2_pads_set.find(link.source_id) == v2_pads_set.end());
			fail_on_test(v2_pads_set.find(link.sink_id) == v2_pads_set.end());
			fail_on_test(link.source_id == link.sink_id);
			num_data_links++;
		}
	}
	node->topology = &topology;
	return 0;
}

static media_link_desc link_immutable;
static media_link_desc link_enabled;
static media_link_desc link_disabled;

int testMediaEnum(struct node *node)
{
	typedef std::map<__u32, media_entity_desc> entity_map;
	entity_map ent_map;
	id_set has_default_set;
	struct media_entity_desc ent;
	struct media_links_enum links;
	unsigned num_links = 0;
	__u32 last_id = 0;
	int ret;

	memset(&ent, 0, sizeof(ent));
	// Entity ID 0 can never occur
	ent.id = 0;
	fail_on_test(doioctl(node, MEDIA_IOC_ENUM_ENTITIES, &ent) != EINVAL);
	for (;;) {
		memset(&ent, 0xff, sizeof(ent));
		ent.id = last_id | MEDIA_ENT_ID_FLAG_NEXT;
		ret = doioctl(node, MEDIA_IOC_ENUM_ENTITIES, &ent);
		if (ret == EINVAL)
			break;
		fail_on_test(check_0(ent.reserved, sizeof(ent.reserved)));
		fail_on_test(ent.id & MEDIA_ENT_ID_FLAG_NEXT);
		fail_on_test(!ent.id);
		fail_on_test(ent.id <= last_id);
		last_id = ent.id;
		fail_on_test(check_string(ent.name, sizeof(ent.name)));
		fail_on_test(ent.revision);
		fail_on_test(ent.group_id);
		fail_on_test(ent.type == ~0U);
		fail_on_test(ent.type == MEDIA_ENT_F_UNKNOWN);
		// Commented out due to horrible workaround in media-device.c
		//fail_on_test(ent.type == MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN);
		//fail_on_test(ent.type == MEDIA_ENT_T_DEVNODE_UNKNOWN);
		fail_on_test(ent.flags == ~0U);
		fail_on_test(ent.pads == 0xffff);
		fail_on_test(ent.links == 0xffff);

		if (ent.flags & MEDIA_ENT_FL_DEFAULT) {
			fail_on_test(has_default_set.find(ent.type) != has_default_set.end());
			has_default_set.insert(ent.type);
		}
		if (!(ent.flags & MEDIA_ENT_FL_CONNECTOR)) {
			fail_on_test(ent.dev.major == ~0U);
			fail_on_test(ent.dev.minor == ~0U);
			if (ent.dev.major || ent.dev.minor)
				fail_on_test(checkDevice(ent.dev.major, ent.dev.minor,
							 false, ent.id));
		}
		fail_on_test(doioctl(node, MEDIA_IOC_ENUM_ENTITIES, &ent));
		num_links += ent.links;

		fail_on_test(v2_entities_set.find(ent.id) == v2_entities_set.end());
		if (node->topology) {
			fail_on_test(entity_num_pads[ent.id] != ent.pads);
			// Commented out due to horrible workaround in media-device.c
			//fail_on_test(v2_entity_map[ent.id]->function != ent.type);
		}
		ent_map[ent.id] = ent;
	}
	fail_on_test(num_data_links != num_links);

	for (entity_map::iterator iter = ent_map.begin();
	     iter != ent_map.end(); ++iter) {
		media_entity_desc &ent = iter->second;

		memset(&links, 0, sizeof(links));
		links.entity = ent.id;
		fail_on_test(doioctl(node, MEDIA_IOC_ENUM_LINKS, &links));
		fail_on_test(links.entity != ent.id);
		fail_on_test(links.pads);
		fail_on_test(links.links);
		links.pads = (struct media_pad_desc *)4;
		fail_on_test(ent.pads && doioctl(node, MEDIA_IOC_ENUM_LINKS, &links) != EFAULT);
		links.pads = NULL;
		links.links = (struct media_link_desc *)4;
		fail_on_test(ent.links && doioctl(node, MEDIA_IOC_ENUM_LINKS, &links) != EFAULT);
		links.links = NULL;
		links.pads = new media_pad_desc[ent.pads];
		memset(links.pads, 0xff, ent.pads * sizeof(*links.pads));
		links.links = new media_link_desc[ent.links];
		memset(links.links, 0xff, ent.links * sizeof(*links.links));
		fail_on_test(doioctl(node, MEDIA_IOC_ENUM_LINKS, &links));

		bool found_source = false;
		for (unsigned i = 0; i < ent.pads; i++) {
			fail_on_test(links.pads[i].entity != ent.id);
			fail_on_test(links.pads[i].index != i);
			fail_on_test(check_0(links.pads[i].reserved, sizeof(links.pads[i].reserved)));
			__u32 fl = links.pads[i].flags;
			fail_on_test(!(fl & (MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_SOURCE)));
			fail_on_test((fl & (MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_SOURCE)) ==
				     (MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_SOURCE));
			if (fl & MEDIA_PAD_FL_SOURCE)
				found_source = true;
			else if (fl & MEDIA_PAD_FL_SINK)
				fail_on_test(found_source);
		}
		bool found_enabled = false;
		for (unsigned i = 0; i < ent.links; i++) {
			bool is_sink = links.links[i].sink.entity == ent.id;
			__u32 fl = links.links[i].flags;
			__u32 remote_ent;
			__u16 remote_pad;

			fail_on_test(links.links[i].source.entity != ent.id &&
				     links.links[i].sink.entity != ent.id);
			fail_on_test(check_0(links.links[i].reserved, sizeof(links.links[i].reserved)));
			if (fl & MEDIA_LNK_FL_IMMUTABLE) {
				fail_on_test(!(fl & MEDIA_LNK_FL_ENABLED));
				fail_on_test(fl & MEDIA_LNK_FL_DYNAMIC);
				if (!link_immutable.source.entity)
					link_immutable = links.links[i];
			}
			if (fl & MEDIA_LNK_FL_DYNAMIC)
				fail_on_test(!(fl & MEDIA_LNK_FL_IMMUTABLE));
			if (is_sink && (fl & MEDIA_LNK_FL_ENABLED)) {
				// only one incoming link can be enabled
				fail_on_test(found_enabled);
				found_enabled = true;
			}
			if ((fl & MEDIA_LNK_FL_ENABLED) && !link_enabled.source.entity)
				link_enabled = links.links[i];
			if (!(fl & (MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED)) &&
			    !link_disabled.source.entity)
				link_disabled = links.links[i];

			// This ioctl only returns data links
			fail_on_test(fl & MEDIA_LNK_FL_LINK_TYPE);
			fail_on_test(links.links[i].sink.entity == links.links[i].source.entity);
			if (is_sink) {
				fail_on_test(links.links[i].sink.index >= ent.pads);
				remote_ent = links.links[i].source.entity;
				remote_pad = links.links[i].source.index;
			} else {
				fail_on_test(links.links[i].source.index >= ent.pads);
				remote_ent = links.links[i].sink.entity;
				remote_pad = links.links[i].sink.index;
			}
			fail_on_test(ent_map.find(remote_ent) == ent_map.end());
			media_entity_desc &remote = ent_map[remote_ent];
			fail_on_test(remote_pad >= remote.pads);
		}
	}

	memset(&links, 0, sizeof(links));
	fail_on_test(doioctl(node, MEDIA_IOC_ENUM_LINKS, &links) != EINVAL);

	for (unsigned i = 0; i < topology.num_entities; i++)
		fail_on_test(ent_map.find(v2_ents[i].id) == ent_map.end());
	return 0;
}

int testMediaSetupLink(struct node *node)
{
	struct media_link_desc link;
	int ret;

	memset(&link, 0, sizeof(link));
	ret = doioctl(node, MEDIA_IOC_SETUP_LINK, &link);
	if (ret == ENOTTY)
		return ret;
	fail_on_test(ret != EINVAL);
	if (link_immutable.source.entity) {
		link = link_immutable;
		fail_on_test(doioctl(node, MEDIA_IOC_SETUP_LINK, &link));
		link.flags = MEDIA_LNK_FL_ENABLED;
		fail_on_test(doioctl(node, MEDIA_IOC_SETUP_LINK, &link) != EINVAL);
	}
	if (link_disabled.source.entity) {
		link = link_disabled;
		memset(link.reserved, 0xff, sizeof(link.reserved));
		fail_on_test(doioctl(node, MEDIA_IOC_SETUP_LINK, &link));
		//fail_on_test(check_0(link.reserved, sizeof(link.reserved)));
		link.flags |= MEDIA_LNK_FL_INTERFACE_LINK;
		fail_on_test(doioctl(node, MEDIA_IOC_SETUP_LINK, &link) != EINVAL);
	}
	if (link_enabled.source.entity) {
		link = link_enabled;
		memset(link.reserved, 0xff, sizeof(link.reserved));
		fail_on_test(doioctl(node, MEDIA_IOC_SETUP_LINK, &link));
		//fail_on_test(check_0(link.reserved, sizeof(link.reserved)));
		link.flags |= MEDIA_LNK_FL_INTERFACE_LINK;
		fail_on_test(doioctl(node, MEDIA_IOC_SETUP_LINK, &link) != EINVAL);
	}
	return 0;
}

void walkTopology(struct node &node, struct node &expbuf_node, unsigned frame_count)
{
	if (!node.topology)
		return;

	for (unsigned i = 0; i < node.topology->num_interfaces; i++) {
		media_v2_interface &iface = v2_ifaces[i];
		std::string dev = media_get_device(iface.devnode.major,
						   iface.devnode.minor);
		if (dev.empty())
			continue;
		media_type type = media_detect_type(dev.c_str());
		if (type == MEDIA_TYPE_CANT_STAT) {
			fprintf(stderr, "Cannot open device %s, skipping.\n\n",
				dev.c_str());
			continue;
		}

		switch (type) {
		// For now we can only handle V4L2 devices
		case MEDIA_TYPE_VIDEO:
		case MEDIA_TYPE_VBI:
		case MEDIA_TYPE_RADIO:
		case MEDIA_TYPE_SDR:
		case MEDIA_TYPE_TOUCH:
		case MEDIA_TYPE_SUBDEV:
			break;
		default:
			type = MEDIA_TYPE_UNKNOWN;
			break;
		}

		if (type == MEDIA_TYPE_UNKNOWN) {
			fprintf(stderr, "Unable to detect what device %s is, skipping.\n\n",
				dev.c_str());
			continue;
		}

		struct node test_node;
		int fd = -1;

		test_node.device = dev.c_str();
		test_node.s_trace(node.g_trace());
		switch (type) {
		case MEDIA_TYPE_MEDIA:
			test_node.s_direct(true);
			fd = test_node.media_open(dev.c_str(), false);
			break;
		case MEDIA_TYPE_SUBDEV:
			test_node.s_direct(true);
			fd = test_node.subdev_open(dev.c_str(), false);
			break;
		default:
			test_node.s_direct(node.g_direct());
			fd = test_node.open(dev.c_str(), false);
			break;
		}
		if (fd < 0) {
			fprintf(stderr, "Failed to open device %s, skipping\n\n",
				dev.c_str());
			continue;
		}

		testNode(test_node, expbuf_node, type, frame_count);
		test_node.close();
	}
}
