/*
 ** packet-netflow.c
 ** 
 *****************************************************************************
 ** (c) 2002 bill fumerola <fumerola@yahoo-inc.com>
 ** All rights reserved.
 ** 
 ** This program is free software; you can redistribute it and/or
 ** modify it under the terms of the GNU General Public License
 ** as published by the Free Software Foundation; either version 2
 ** of the License, or (at your option) any later version.
 ** 
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 ** 
 ** You should have received a copy of the GNU General Public License
 ** along with this program; if not, write to the Free Software
 ** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *****************************************************************************
 **
 ** previous netflow dissector written by Matthew Smart <smart@monkey.org>
 **
 *****************************************************************************
 **
 ** this code was written from the following documentation:
 **
 ** http://www.cisco.com/univercd/cc/td/doc/product/rtrmgmt/nfc/nfc_3_6/iug/format.pdf
 ** http://www.caida.org/tools/measurement/cflowd/configuration/configuration-9.html
 **
 ** some documentation is more accurate then others. in some cases, live data and
 ** information contained in responses from vendors were also used. some fields
 ** are dissected as vendor specific fields.
 **
 ** $Yahoo: //depot/fumerola/packet-netflow/packet-netflow.c#14 $
 ** $Id: packet-netflow.c,v 1.6 2002/10/08 08:50:04 guy Exp $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <epan/packet.h>
#include <string.h>

#define UDP_PORT_NETFLOW	2055

/*
 * pdu identifiers & sizes 
 */

#define V1PDU_SIZE		(4 * 12)
#define V5PDU_SIZE		(4 * 12)
#define V7PDU_SIZE		(4 * 13)
#define V8PDU_AS_SIZE		(4 * 7)
#define V8PDU_PROTO_SIZE	(4 * 7)
#define V8PDU_SPREFIX_SIZE	(4 * 8)
#define V8PDU_DPREFIX_SIZE	(4 * 8)
#define V8PDU_MATRIX_SIZE	(4 * 10)
#define V8PDU_DESTONLY_SIZE	(4 * 8)
#define V8PDU_SRCDEST_SIZE	(4 * 10)
#define V8PDU_FULL_SIZE		(4 * 11)
#define V8PDU_TOSAS_SIZE	(V8PDU_AS_SIZE + 4)
#define V8PDU_TOSPROTOPORT_SIZE	(V8PDU_PROTO_SIZE + 4)
#define V8PDU_TOSSRCPREFIX_SIZE	V8PDU_SPREFIX_SIZE
#define V8PDU_TOSDSTPREFIX_SIZE	V8PDU_DPREFIX_SIZE
#define V8PDU_TOSMATRIX_SIZE	V8PDU_MATRIX_SIZE
#define V8PDU_PREPORTPROTOCOL_SIZE (4 * 10)

enum {
	V8PDU_NO_METHOD = 0,
	V8PDU_AS_METHOD,
	V8PDU_PROTO_METHOD,
	V8PDU_SPREFIX_METHOD,
	V8PDU_DPREFIX_METHOD,
	V8PDU_MATRIX_METHOD,
	V8PDU_DESTONLY_METHOD,
	V8PDU_SRCDEST_METHOD,
	V8PDU_FULL_METHOD,
	V8PDU_TOSAS_METHOD,
	V8PDU_TOSPROTOPORT_METHOD,
	V8PDU_TOSSRCPREFIX_METHOD,
	V8PDU_TOSDSTPREFIX_METHOD,
	V8PDU_TOSMATRIX_METHOD,
	V8PDU_PREPORTPROTOCOL_METHOD
};

static const value_string v8_agg[] = {
	{V8PDU_AS_METHOD, "V8 AS aggregation"},
	{V8PDU_PROTO_METHOD, "V8 Proto/Port aggregation"},
	{V8PDU_SPREFIX_METHOD, "V8 Source Prefix aggregation"},
	{V8PDU_DPREFIX_METHOD, "V8 Destination Prefix aggregation"},
	{V8PDU_MATRIX_METHOD, "V8 Network Matrix aggregation"},
	{V8PDU_DESTONLY_METHOD, "V8 Destination aggregation (Cisco Catalyst)"},
	{V8PDU_SRCDEST_METHOD, "V8 Src/Dest aggregation (Cisco Catalyst)"},
	{V8PDU_FULL_METHOD, "V8 Full aggregation (Cisco Catalyst)"},
	{V8PDU_TOSAS_METHOD, "V8 TOS+AS aggregation aggregation"},
	{V8PDU_TOSPROTOPORT_METHOD, "V8 TOS+Protocol aggregation"},
	{V8PDU_TOSSRCPREFIX_METHOD, "V8 TOS+Source Prefix aggregation"},
	{V8PDU_TOSDSTPREFIX_METHOD, "V8 TOS+Destination Prefix aggregation"},
	{V8PDU_TOSMATRIX_METHOD, "V8 TOS+Prefix Matrix aggregation"},
	{V8PDU_PREPORTPROTOCOL_METHOD, "V8 Port+Protocol aggregation"},
	{0, NULL}
};


/*
 * ethereal tree identifiers
 */

static int      proto_netflow = -1;
static int      ett_netflow = -1;
static int      ett_unixtime = -1;
static int      ett_flow = -1;

/*
 * cflow header 
 */

static int      hf_cflow_version = -1;
static int      hf_cflow_count = -1;
static int      hf_cflow_sysuptime = -1;
static int      hf_cflow_unix_secs = -1;
static int      hf_cflow_unix_nsecs = -1;
static int      hf_cflow_timestamp = -1;
static int      hf_cflow_samplerate = -1;

/*
 * cflow version specific info 
 */
static int      hf_cflow_sequence = -1;
static int      hf_cflow_engine_type = -1;
static int      hf_cflow_engine_id = -1;

static int      hf_cflow_aggmethod = -1;
static int      hf_cflow_aggversion = -1;

/*
 * pdu storage
 */
static int      hf_cflow_srcaddr = -1;
static int      hf_cflow_srcnet = -1;
static int      hf_cflow_dstaddr = -1;
static int      hf_cflow_dstnet = -1;
static int      hf_cflow_nexthop = -1;
static int      hf_cflow_inputint = -1;
static int      hf_cflow_outputint = -1;
static int      hf_cflow_flows = -1;
static int      hf_cflow_packets = -1;
static int      hf_cflow_octets = -1;
static int      hf_cflow_timestart = -1;
static int      hf_cflow_timeend = -1;
static int      hf_cflow_srcport = -1;
static int      hf_cflow_dstport = -1;
static int      hf_cflow_prot = -1;
static int      hf_cflow_tos = -1;
static int      hf_cflow_flags = -1;
static int      hf_cflow_tcpflags = -1;
static int      hf_cflow_dstas = -1;
static int      hf_cflow_srcas = -1;
static int      hf_cflow_dstmask = -1;
static int      hf_cflow_srcmask = -1;
static int      hf_cflow_routersc = -1;

typedef int     dissect_pdu_t(proto_tree * pdutree, tvbuff_t * tvb, int offset,
			      int verspec);
static int      dissect_pdu(proto_tree * tree, tvbuff_t * tvb, int offset,
			    int verspec);
static int      dissect_v8_aggpdu(proto_tree * pdutree, tvbuff_t * tvb,
				  int offset, int verspec);
static int      dissect_v8_flowpdu(proto_tree * pdutree, tvbuff_t * tvb,
				   int offset, int verspec);

static gchar   *getprefix(const guint32 * address, int prefix);
static void     dissect_netflow(tvbuff_t * tvb, packet_info * pinfo,
				proto_tree * tree);

static int      flow_process_ints(proto_tree * pdutree, tvbuff_t * tvb,
				  int offset);
static int      flow_process_ports(proto_tree * pdutree, tvbuff_t * tvb,
				   int offset);
static int      flow_process_timeperiod(proto_tree * pdutree, tvbuff_t * tvb,
					int offset);
static int      flow_process_aspair(proto_tree * pdutree, tvbuff_t * tvb,
				    int offset);
static int      flow_process_sizecount(proto_tree * pdutree, tvbuff_t * tvb,
				       int offset);
static int      flow_process_textfield(proto_tree * pdutree, tvbuff_t * tvb,
				       int offset, int bytes,
				       const char *text);


static void
dissect_netflow(tvbuff_t * tvb, packet_info * pinfo, proto_tree * tree)
{
	proto_tree     *netflow_tree = NULL;
	proto_tree     *ti;
	proto_item     *timeitem, *pduitem;
	proto_tree     *timetree, *pdutree;
	unsigned int    pduret, ver = 0, pdus = 0, x = 1, vspec;
	size_t          available, pdusize, offset = 0;
	nstime_t        ts;
	dissect_pdu_t  *pduptr;

	if (check_col(pinfo->cinfo, COL_PROTOCOL))
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "CFLOW");
	if (check_col(pinfo->cinfo, COL_INFO))
		col_clear(pinfo->cinfo, COL_INFO);

	if (tree) {
		ti = proto_tree_add_item(tree, proto_netflow, tvb,
					 offset, -1, FALSE);
		netflow_tree = proto_item_add_subtree(ti, ett_netflow);
	}

	ver = tvb_get_ntohs(tvb, offset);
	vspec = ver;
	switch (ver) {
	case 1:
		pdusize = V1PDU_SIZE;
		pduptr = &dissect_pdu;
		break;
	case 5:
		pdusize = V5PDU_SIZE;
		pduptr = &dissect_pdu;
		break;
	case 7:
		pdusize = V7PDU_SIZE;
		pduptr = &dissect_pdu;
		break;
	case 8:
		pdusize = -1;	/* deferred */
		pduptr = &dissect_v8_aggpdu;
		break;
	default:
		return;
	}

	if (tree)
		proto_tree_add_uint(netflow_tree, hf_cflow_version, tvb,
				    offset, 2, ver);
	offset += 2;

	pdus = tvb_get_ntohs(tvb, offset);
	if (pdus <= 0)
		return;
	if (tree)
		proto_tree_add_uint(netflow_tree, hf_cflow_count, tvb,
				    offset, 2, pdus);
	offset += 2;

	/*
	 * set something interesting in the display now that we have info 
	 */
	if (check_col(pinfo->cinfo, COL_INFO))
		col_add_fstr(pinfo->cinfo, COL_INFO, "total: %u (v%u) flows",
			     pdus, ver);

	/*
	 * the rest is only interesting if we're displaying/searching the
	 * packet 
	 */
	if (!tree)
		return;

	proto_tree_add_item(netflow_tree, hf_cflow_sysuptime, tvb,
			    offset, 4, FALSE);
	offset += 4;

	ts.secs = tvb_get_ntohl(tvb, offset);
	ts.nsecs = tvb_get_ntohl(tvb, offset + 4);
	timeitem = proto_tree_add_time(netflow_tree,
				       hf_cflow_timestamp, tvb, offset,
				       8, &ts);
	timetree = proto_item_add_subtree(timeitem, ett_unixtime);

	proto_tree_add_item(timetree, hf_cflow_unix_secs, tvb,
			    offset, 4, FALSE);
	offset += 4;

	proto_tree_add_item(timetree, hf_cflow_unix_nsecs, tvb,
			    offset, 4, FALSE);
	offset += 4;

	/*
	 * version specific header 
	 */
	if (ver == 5 || ver == 7 || ver == 8) {
		proto_tree_add_item(netflow_tree, hf_cflow_sequence,
				    tvb, offset, 4, FALSE);
		offset += 4;
	}
	if (ver == 5 || ver == 8) {
		proto_tree_add_item(netflow_tree, hf_cflow_engine_type,
				    tvb, offset++, 1, FALSE);
		proto_tree_add_item(netflow_tree, hf_cflow_engine_id,
				    tvb, offset++, 1, FALSE);
	}
	if (ver == 8) {
		vspec = tvb_get_guint8(tvb, offset);
		switch (vspec) {
		case V8PDU_AS_METHOD:
			pdusize = V8PDU_AS_SIZE;
			break;
		case V8PDU_PROTO_METHOD:
			pdusize = V8PDU_PROTO_SIZE;
			break;
		case V8PDU_SPREFIX_METHOD:
			pdusize = V8PDU_SPREFIX_SIZE;
			break;
		case V8PDU_DPREFIX_METHOD:
			pdusize = V8PDU_DPREFIX_SIZE;
			break;
		case V8PDU_MATRIX_METHOD:
			pdusize = V8PDU_MATRIX_SIZE;
			break;
		case V8PDU_DESTONLY_METHOD:
			pdusize = V8PDU_DESTONLY_SIZE;
			pduptr = &dissect_v8_flowpdu;
			break;
		case V8PDU_SRCDEST_METHOD:
			pdusize = V8PDU_SRCDEST_SIZE;
			pduptr = &dissect_v8_flowpdu;
			break;
		case V8PDU_FULL_METHOD:
			pdusize = V8PDU_FULL_SIZE;
			pduptr = &dissect_v8_flowpdu;
			break;
		case V8PDU_TOSAS_METHOD:
			pdusize = V8PDU_TOSAS_SIZE;
			break;
		case V8PDU_TOSPROTOPORT_METHOD:
			pdusize = V8PDU_TOSPROTOPORT_SIZE;
			break;
		case V8PDU_TOSSRCPREFIX_METHOD:
			pdusize = V8PDU_TOSSRCPREFIX_SIZE;
			break;
		case V8PDU_TOSDSTPREFIX_METHOD:
			pdusize = V8PDU_TOSDSTPREFIX_SIZE;
			break;
		case V8PDU_TOSMATRIX_METHOD:
			pdusize = V8PDU_TOSMATRIX_SIZE;
			break;
		case V8PDU_PREPORTPROTOCOL_METHOD:
			pdusize = V8PDU_PREPORTPROTOCOL_SIZE;
			break;
		default:
			pdusize = -1;
			vspec = 0;
			break;
		}
		proto_tree_add_uint(netflow_tree, hf_cflow_aggmethod,
				    tvb, offset++, 1, vspec);
		proto_tree_add_item(netflow_tree, hf_cflow_aggversion,
				    tvb, offset++, 1, FALSE);
	}
	if (ver == 7 || ver == 8)
		offset = flow_process_textfield(netflow_tree, tvb, offset, 4,
						"reserved");
	else if (ver == 5) {
		proto_tree_add_item(netflow_tree, hf_cflow_samplerate,
				    tvb, offset, 2, FALSE);
		offset += 2;
	}

	/*
	 * everything below here should be payload 
	 */
	for (x = 1; x < pdus + 1; x++) {
		/*
		 * make sure we have a pdu's worth of data 
		 */
		available = tvb_length_remaining(tvb, offset);
		if (available < pdusize)
			break;

		pduitem =
		    proto_tree_add_text(netflow_tree, tvb, offset, pdusize,
					"pdu %u/%u", x, pdus);
		pdutree = proto_item_add_subtree(pduitem, ett_flow);

		pduret = pduptr(pdutree, tvb, offset, vspec);

		/*
		 * if we came up short, stop processing 
		 */
		if (pduret == pdusize)
			offset += pduret;
		else
			break;
	}

	return;
}

/*
 * flow_process_* == common groups of fields, probably could be inline 
 */

static int
flow_process_ints(proto_tree * pdutree, tvbuff_t * tvb, int offset)
{
	proto_tree_add_item(pdutree, hf_cflow_inputint, tvb, offset, 2, FALSE);
	offset += 2;

	proto_tree_add_item(pdutree, hf_cflow_outputint, tvb, offset, 2,
			    FALSE);
	offset += 2;

	return offset;
}

static int
flow_process_ports(proto_tree * pdutree, tvbuff_t * tvb, int offset)
{
	proto_tree_add_item(pdutree, hf_cflow_srcport, tvb, offset, 2, FALSE);
	offset += 2;

	proto_tree_add_item(pdutree, hf_cflow_dstport, tvb, offset, 2, FALSE);
	offset += 2;

	return offset;
}

static int
flow_process_timeperiod(proto_tree * pdutree, tvbuff_t * tvb, int offset)
{
	nstime_t        ts;

	ts.secs = tvb_get_ntohl(tvb, offset) / 1000;
	ts.nsecs = ((tvb_get_ntohl(tvb, offset) % 1000) * 1000000);
	proto_tree_add_time(pdutree, hf_cflow_timestart, tvb, offset, 4, &ts);
	offset += 4;

	ts.secs = tvb_get_ntohl(tvb, offset) / 1000;
	ts.nsecs = ((tvb_get_ntohl(tvb, offset) % 1000) * 1000000);
	proto_tree_add_time(pdutree, hf_cflow_timeend, tvb, offset, 4, &ts);
	offset += 4;

	return offset;
}


static int
flow_process_aspair(proto_tree * pdutree, tvbuff_t * tvb, int offset)
{
	proto_tree_add_item(pdutree, hf_cflow_srcas, tvb, offset, 2, FALSE);
	offset += 2;

	proto_tree_add_item(pdutree, hf_cflow_dstas, tvb, offset, 2, FALSE);
	offset += 2;

	return offset;
}

static int
flow_process_sizecount(proto_tree * pdutree, tvbuff_t * tvb, int offset)
{
	proto_tree_add_item(pdutree, hf_cflow_packets, tvb, offset, 4, FALSE);
	offset += 4;

	proto_tree_add_item(pdutree, hf_cflow_octets, tvb, offset, 4, FALSE);
	offset += 4;

	return offset;
}

static int
flow_process_textfield(proto_tree * pdutree, tvbuff_t * tvb, int offset,
		       int bytes, const char *text)
{
	proto_tree_add_text(pdutree, tvb, offset, bytes, text);
	offset += bytes;

	return offset;
}

static int
dissect_v8_flowpdu(proto_tree * pdutree, tvbuff_t * tvb, int offset,
		   int verspec)
{
	int             startoffset = offset;

	proto_tree_add_item(pdutree, hf_cflow_dstaddr, tvb, offset, 4, FALSE);
	offset += 4;

	if (verspec != V8PDU_DESTONLY_METHOD) {
		proto_tree_add_item(pdutree, hf_cflow_srcaddr, tvb, offset, 4,
				    FALSE);
		offset += 4;
	}
	if (verspec == V8PDU_FULL_METHOD) {
		proto_tree_add_item(pdutree, hf_cflow_dstport, tvb, offset, 2,
				    FALSE);
		offset += 2;
		proto_tree_add_item(pdutree, hf_cflow_srcport, tvb, offset, 2,
				    FALSE);
		offset += 2;
	}

	offset = flow_process_sizecount(pdutree, tvb, offset);
	offset = flow_process_timeperiod(pdutree, tvb, offset);

	proto_tree_add_item(pdutree, hf_cflow_outputint, tvb, offset, 2,
			    FALSE);
	offset += 2;

	if (verspec != V8PDU_DESTONLY_METHOD) {
		proto_tree_add_item(pdutree, hf_cflow_inputint, tvb, offset, 2,
				    FALSE);
		offset += 2;
	}

	proto_tree_add_item(pdutree, hf_cflow_tos, tvb, offset++, 1, FALSE);
	if (verspec == V8PDU_FULL_METHOD)
		proto_tree_add_item(pdutree, hf_cflow_prot, tvb, offset++, 1,
				    FALSE);
	offset = flow_process_textfield(pdutree, tvb, offset, 1, "marked tos");

	if (verspec == V8PDU_SRCDEST_METHOD)
		offset =
		    flow_process_textfield(pdutree, tvb, offset, 2,
					   "reserved");
	else if (verspec == V8PDU_FULL_METHOD)
		offset =
		    flow_process_textfield(pdutree, tvb, offset, 1, "padding");

	offset =
	    flow_process_textfield(pdutree, tvb, offset, 4, "extra packets");

	proto_tree_add_item(pdutree, hf_cflow_routersc, tvb, offset, 4, FALSE);
	offset += 4;

	return (offset - startoffset);
}

/*
 * dissect a version 8 pdu, returning the length of the pdu processed 
 */

static int
dissect_v8_aggpdu(proto_tree * pdutree, tvbuff_t * tvb, int offset,
		  int verspec)
{
	int             startoffset = offset;

	proto_tree_add_item(pdutree, hf_cflow_flows, tvb, offset, 4, FALSE);
	offset += 4;

	offset = flow_process_sizecount(pdutree, tvb, offset);
	offset = flow_process_timeperiod(pdutree, tvb, offset);

	switch (verspec) {
	case V8PDU_AS_METHOD:
	case V8PDU_TOSAS_METHOD:
		offset = flow_process_aspair(pdutree, tvb, offset);

		if (verspec == V8PDU_TOSAS_METHOD) {
			proto_tree_add_item(pdutree, hf_cflow_tos, tvb,
					    offset++, 1, FALSE);
			offset =
			    flow_process_textfield(pdutree, tvb, offset, 1,
						   "padding");
			offset =
			    flow_process_textfield(pdutree, tvb, offset, 2,
						   "reserved");
		}
		break;
	case V8PDU_PROTO_METHOD:
	case V8PDU_TOSPROTOPORT_METHOD:
		proto_tree_add_item(pdutree, hf_cflow_prot, tvb, offset++, 1,
				    FALSE);

		if (verspec == V8PDU_PROTO_METHOD)
			offset =
			    flow_process_textfield(pdutree, tvb, offset, 1,
						   "padding");
		else if (verspec == V8PDU_TOSPROTOPORT_METHOD)
			proto_tree_add_item(pdutree, hf_cflow_tos, tvb,
					    offset++, 1, FALSE);

		offset =
		    flow_process_textfield(pdutree, tvb, offset, 2,
					   "reserved");
		offset = flow_process_ports(pdutree, tvb, offset);

		if (verspec == V8PDU_TOSPROTOPORT_METHOD)
			offset = flow_process_ints(pdutree, tvb, offset);
		break;
	case V8PDU_SPREFIX_METHOD:
	case V8PDU_DPREFIX_METHOD:
	case V8PDU_TOSSRCPREFIX_METHOD:
	case V8PDU_TOSDSTPREFIX_METHOD:
		proto_tree_add_item(pdutree,
				    verspec ==
				    V8PDU_SPREFIX_METHOD ?
				    hf_cflow_srcnet : hf_cflow_dstnet, tvb,
				    offset, 4, FALSE);
		offset += 4;

		proto_tree_add_item(pdutree,
				    verspec ==
				    V8PDU_SPREFIX_METHOD ?
				    hf_cflow_srcmask : hf_cflow_dstmask, tvb,
				    offset++, 1, FALSE);

		if (verspec == V8PDU_SPREFIX_METHOD
		    || verspec == V8PDU_DPREFIX_METHOD)
			offset =
			    flow_process_textfield(pdutree, tvb, offset, 1,
						   "padding");
		else if (verspec == V8PDU_TOSSRCPREFIX_METHOD
			 || verspec == V8PDU_TOSDSTPREFIX_METHOD)
			proto_tree_add_item(pdutree, hf_cflow_tos, tvb,
					    offset++, 1, FALSE);

		proto_tree_add_item(pdutree,
				    verspec ==
				    V8PDU_SPREFIX_METHOD ? hf_cflow_srcas
				    : hf_cflow_dstas, tvb, offset, 2, FALSE);
		offset += 2;

		proto_tree_add_item(pdutree,
				    verspec ==
				    V8PDU_SPREFIX_METHOD ?
				    hf_cflow_inputint : hf_cflow_outputint,
				    tvb, offset, 2, FALSE);
		offset += 2;

		offset =
		    flow_process_textfield(pdutree, tvb, offset, 2,
					   "reserved");
		break;
	case V8PDU_MATRIX_METHOD:
	case V8PDU_TOSMATRIX_METHOD:
	case V8PDU_PREPORTPROTOCOL_METHOD:
		proto_tree_add_item(pdutree, hf_cflow_srcnet, tvb, offset, 4,
				    FALSE);
		offset += 4;

		proto_tree_add_item(pdutree, hf_cflow_dstnet, tvb, offset, 4,
				    FALSE);
		offset += 4;

		proto_tree_add_item(pdutree, hf_cflow_srcmask, tvb, offset++,
				    1, FALSE);

		proto_tree_add_item(pdutree, hf_cflow_dstmask, tvb, offset++,
				    1, FALSE);

		if (verspec == V8PDU_TOSMATRIX_METHOD ||
		    verspec == V8PDU_PREPORTPROTOCOL_METHOD) {
			proto_tree_add_item(pdutree, hf_cflow_tos, tvb,
					    offset++, 1, FALSE);
			if (verspec == V8PDU_TOSMATRIX_METHOD) {
				offset =
				    flow_process_textfield(pdutree, tvb,
							   offset, 1,
							   "padding");
			} else if (verspec == V8PDU_PREPORTPROTOCOL_METHOD) {
				proto_tree_add_item(pdutree, hf_cflow_prot,
						    tvb, offset++, 1, FALSE);
			}
		} else {
			offset =
			    flow_process_textfield(pdutree, tvb, offset, 2,
						   "reserved");
		}

		if (verspec == V8PDU_MATRIX_METHOD
		    || verspec == V8PDU_TOSMATRIX_METHOD) {
			offset = flow_process_aspair(pdutree, tvb, offset);
		} else if (verspec == V8PDU_PREPORTPROTOCOL_METHOD) {
			offset = flow_process_ports(pdutree, tvb, offset);
		}

		offset = flow_process_ints(pdutree, tvb, offset);
		break;
	}


	return (offset - startoffset);
}

/*
 * dissect a version 1, 5, or 7 pdu and return the length of the pdu we
 * processed
 */

static int
dissect_pdu(proto_tree * pdutree, tvbuff_t * tvb, int offset, int ver)
{
	int             startoffset = offset;
	guint32         srcaddr, dstaddr;
	guint8          mask;
	nstime_t        ts;

	memset(&ts, '\0', sizeof(ts));

	/*
	 * memcpy so we can use the values later to calculate a prefix 
	 */
	tvb_memcpy(tvb, (guint8 *) & srcaddr, offset, 4);
	proto_tree_add_ipv4(pdutree, hf_cflow_srcaddr, tvb, offset, 4,
			    srcaddr);
	offset += 4;

	tvb_memcpy(tvb, (guint8 *) & dstaddr, offset, 4);
	proto_tree_add_ipv4(pdutree, hf_cflow_dstaddr, tvb, offset, 4,
			    dstaddr);
	offset += 4;

	proto_tree_add_item(pdutree, hf_cflow_nexthop, tvb, offset, 4, FALSE);
	offset += 4;

	offset = flow_process_ints(pdutree, tvb, offset);
	offset = flow_process_sizecount(pdutree, tvb, offset);
	offset = flow_process_timeperiod(pdutree, tvb, offset);
	offset = flow_process_ports(pdutree, tvb, offset);

	/*
	 * and the similarities end here 
	 */
	if (ver == 1) {
		offset =
		    flow_process_textfield(pdutree, tvb, offset, 2, "padding");

		proto_tree_add_item(pdutree, hf_cflow_prot, tvb, offset++, 1,
				    FALSE);

		proto_tree_add_item(pdutree, hf_cflow_tos, tvb, offset++, 1,
				    FALSE);

		proto_tree_add_item(pdutree, hf_cflow_tcpflags, tvb, offset++,
				    1, FALSE);

		offset =
		    flow_process_textfield(pdutree, tvb, offset, 3, "padding");

		offset =
		    flow_process_textfield(pdutree, tvb, offset, 4,
					   "reserved");
	} else {
		if (ver == 5)
			offset =
			    flow_process_textfield(pdutree, tvb, offset, 1,
						   "padding");
		else {
			proto_tree_add_item(pdutree, hf_cflow_flags, tvb,
					    offset++, 1, FALSE);
		}

		proto_tree_add_item(pdutree, hf_cflow_tcpflags, tvb, offset++,
				    1, FALSE);

		proto_tree_add_item(pdutree, hf_cflow_prot, tvb, offset++, 1,
				    FALSE);

		proto_tree_add_item(pdutree, hf_cflow_tos, tvb, offset++, 1,
				    FALSE);

		offset = flow_process_aspair(pdutree, tvb, offset);

		mask = tvb_get_guint8(tvb, offset);
		proto_tree_add_text(pdutree, tvb, offset, 1,
				    "SrcMask: %u (prefix: %s/%u)",
				    mask, getprefix(&srcaddr, mask),
				    mask != 0 ? mask : 32);
		proto_tree_add_uint_hidden(pdutree, hf_cflow_srcmask, tvb,
					   offset++, 1, mask);

		mask = tvb_get_guint8(tvb, offset);
		proto_tree_add_text(pdutree, tvb, offset, 1,
				    "DstMask: %u (prefix: %s/%u)",
				    mask, getprefix(&dstaddr, mask),
				    mask != 0 ? mask : 32);
		proto_tree_add_uint_hidden(pdutree, hf_cflow_dstmask, tvb,
					   offset++, 1, mask);

		offset =
		    flow_process_textfield(pdutree, tvb, offset, 2, "padding");

		if (ver == 7) {
			proto_tree_add_item(pdutree, hf_cflow_routersc, tvb,
					    offset, 4, FALSE);
			offset += 4;
		}
	}

	return (offset - startoffset);
}

static gchar   *
getprefix(const guint32 * address, int prefix)
{
	guint32         gprefix;

	gprefix = *address & htonl((0xffffffff << (32 - prefix)));

	return (ip_to_str((const guint8 *)&gprefix));
}

void
proto_register_netflow(void)
{
	static hf_register_info hf[] = {
		/*
		 * flow header 
		 */
		{&hf_cflow_version,
		 {"Version", "cflow.version",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "NetFlow Version", HFILL}
		 },
		{&hf_cflow_count,
		 {"Count", "cflow.count",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Count of PDUs", HFILL}
		 },
		{&hf_cflow_sysuptime,
		 {"SysUptime", "cflow.sysuptime",
		  FT_UINT32, BASE_DEC, NULL, 0x0,
		  "Time since router booted (in milliseconds)", HFILL}
		 },

		{&hf_cflow_timestamp,
		 {"Timestamp", "cflow.timestamp",
		  FT_ABSOLUTE_TIME, BASE_NONE, NULL, 0x0,
		  "Current seconds since epoch", HFILL}
		 },
		{&hf_cflow_unix_secs,
		 {"CurrentSecs", "cflow.unix_secs",
		  FT_UINT32, BASE_DEC, NULL, 0x0,
		  "Current seconds since epoch", HFILL}
		 },
		{&hf_cflow_unix_nsecs,
		 {"CurrentNSecs", "cflow.unix_nsecs",
		  FT_UINT32, BASE_DEC, NULL, 0x0,
		  "Residual nanoseconds since epoch", HFILL}
		 },
		{&hf_cflow_samplerate,
		 {"SampleRate", "cflow.samplerate",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Sample Frequency of exporter", HFILL}
		 },

		/*
		 * end version-agnostic header
		 * version-specific flow header 
		 */
		{&hf_cflow_sequence,
		 {"FlowSequence", "cflow.sequence",
		  FT_UINT32, BASE_DEC, NULL, 0x0,
		  "Sequence number of flows seen", HFILL}
		 },
		{&hf_cflow_engine_type,
		 {"EngineType", "cflow.engine_type",
		  FT_UINT8, BASE_DEC, NULL, 0x0,
		  "Flow switching engine type", HFILL}
		 },
		{&hf_cflow_engine_id,
		 {"EngineId", "cflow.engine_id",
		  FT_UINT8, BASE_DEC, NULL, 0x0,
		  "Slot number of switching engine", HFILL}
		 },
		{&hf_cflow_aggmethod,
		 {"AggMethod", "cflow.aggmethod",
		  FT_UINT8, BASE_DEC, VALS(v8_agg), 0x0,
		  "CFlow V8 Aggregation Method", HFILL}
		 },
		{&hf_cflow_aggversion,
		 {"AggVersion", "cflow.aggversion",
		  FT_UINT8, BASE_DEC, NULL, 0x0,
		  "CFlow V8 Aggregation Version", HFILL}
		 },
		/*
		 * end version specific header storage 
		 */
		/*
		 * begin pdu content storage 
		 */
		{&hf_cflow_srcaddr,
		 {"SrcAddr", "cflow.srcaddr",
		  FT_IPv4, BASE_NONE, NULL, 0x0,
		  "Flow Source Address", HFILL}
		 },
		{&hf_cflow_srcnet,
		 {"SrcNet", "cflow.srcnet",
		  FT_IPv4, BASE_NONE, NULL, 0x0,
		  "Flow Source Network", HFILL}
		 },
		{&hf_cflow_dstaddr,
		 {"DstAddr", "cflow.dstaddr",
		  FT_IPv4, BASE_NONE, NULL, 0x0,
		  "Flow Destination Address", HFILL}
		 },
		{&hf_cflow_dstnet,
		 {"DstNet", "cflow.dstaddr",
		  FT_IPv4, BASE_NONE, NULL, 0x0,
		  "Flow Destination Network", HFILL}
		 },
		{&hf_cflow_nexthop,
		 {"NextHop", "cflow.nexthop",
		  FT_IPv4, BASE_NONE, NULL, 0x0,
		  "Router nexthop", HFILL}
		 },
		{&hf_cflow_inputint,
		 {"InputInt", "cflow.inputint",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Flow Input Interface", HFILL}
		 },
		{&hf_cflow_outputint,
		 {"OutputInt", "cflow.outputint",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Flow Output Interface", HFILL}
		 },
		{&hf_cflow_flows,
		 {"Flows", "cflow.flows",
		  FT_UINT32, BASE_DEC, NULL, 0x0,
		  "Flows Aggregated in PDU", HFILL}
		 },
		{&hf_cflow_packets,
		 {"Packets", "cflow.packets",
		  FT_UINT32, BASE_DEC, NULL, 0x0,
		  "Count of packets", HFILL}
		 },
		{&hf_cflow_octets,
		 {"Octets", "cflow.octets",
		  FT_UINT32, BASE_DEC, NULL, 0x0,
		  "Count of bytes", HFILL}
		 },
		{&hf_cflow_timestart,
		 {"StartTime", "cflow.timestart",
		  FT_RELATIVE_TIME, BASE_NONE, NULL, 0x0,
		  "Uptime at start of flow", HFILL}
		 },
		{&hf_cflow_timeend,
		 {"EndTime", "cflow.timeend",
		  FT_RELATIVE_TIME, BASE_NONE, NULL, 0x0,
		  "Uptime at end of flow", HFILL}
		 },
		{&hf_cflow_srcport,
		 {"SrcPort", "cflow.srcport",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Flow Source Port", HFILL}
		 },
		{&hf_cflow_dstport,
		 {"DstPort", "cflow.dstport",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Flow Destination Port", HFILL}
		 },
		{&hf_cflow_prot,
		 {"Protocol", "cflow.protocol",
		  FT_UINT8, BASE_DEC, NULL, 0x0,
		  "IP Protocol", HFILL}
		 },
		{&hf_cflow_tos,
		 {"IP ToS", "cflow.tos",
		  FT_UINT8, BASE_HEX, NULL, 0x0,
		  "IP Type of Service", HFILL}
		 },
		{&hf_cflow_flags,
		 {"Export Flags", "cflow.flags",
		  FT_UINT8, BASE_HEX, NULL, 0x0,
		  "CFlow Flags", HFILL}
		 },
		{&hf_cflow_tcpflags,
		 {"TCP Flags", "cflow.tcpflags",
		  FT_UINT8, BASE_HEX, NULL, 0x0,
		  "TCP Flags", HFILL}
		 },
		{&hf_cflow_srcas,
		 {"SrcAS", "cflow.srcas",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Source AS", HFILL}
		 },
		{&hf_cflow_dstas,
		 {"DstAS", "cflow.dstas",
		  FT_UINT16, BASE_DEC, NULL, 0x0,
		  "Destination AS", HFILL}
		 },
		{&hf_cflow_srcmask,
		 {"SrcMask", "cflow.srcmask",
		  FT_UINT8, BASE_DEC, NULL, 0x0,
		  "Source Prefix Mask", HFILL}
		 },
		{&hf_cflow_dstmask,
		 {"DstMask", "cflow.dstmask",
		  FT_UINT8, BASE_DEC, NULL, 0x0,
		  "Destination Prefix Mask", HFILL}
		 },
		{&hf_cflow_routersc,
		 {"Router Shortcut", "cflow.routersc",
		  FT_IPv4, BASE_NONE, NULL, 0x0,
		  "Router shortcut by switch", HFILL}
		 }
		/*
		 * end pdu content storage 
		 */
	};

	static gint    *ett[] = {
		&ett_netflow,
		&ett_unixtime,
		&ett_flow
	};

	proto_netflow = proto_register_protocol("Cisco NetFlow", "CFLOW",
						"cflow");

	proto_register_field_array(proto_netflow, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));

	register_dissector("cflow", dissect_netflow, proto_netflow);
}


/*
 * protocol/port association 
 */
void
proto_reg_handoff_netflow(void)
{
	dissector_handle_t netflow_handle;

	netflow_handle = create_dissector_handle(dissect_netflow,
						 proto_netflow);
	dissector_add("udp.port", UDP_PORT_NETFLOW, netflow_handle);
}
