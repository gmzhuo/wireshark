/* packet-arp.c
 * Routines for ARP packet disassembly
 *
 * $Id: packet-arp.c,v 1.10 1998/11/17 04:28:49 gerald Exp $
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@zing.org>
 * Copyright 1998 Gerald Combs
 *
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <gtk/gtk.h>

#include <stdio.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif

#include "ethereal.h"
#include "packet.h"
#include "etypes.h"

/* Definitions taken from Linux "linux/if_arp.h" header file, and from

	http://www.isi.edu/in-notes/iana/assignments/arp-parameters

 */

/* ARP protocol HARDWARE identifiers. */
#define ARPHRD_NETROM	0		/* from KA9Q: NET/ROM pseudo	*/
#define ARPHRD_ETHER 	1		/* Ethernet 10Mbps		*/
#define	ARPHRD_EETHER	2		/* Experimental Ethernet	*/
#define	ARPHRD_AX25	3		/* AX.25 Level 2		*/
#define	ARPHRD_PRONET	4		/* PROnet token ring		*/
#define	ARPHRD_CHAOS	5		/* Chaosnet			*/
#define	ARPHRD_IEEE802	6		/* IEEE 802.2 Ethernet/TR/TB	*/
#define	ARPHRD_ARCNET	7		/* ARCnet			*/
#define	ARPHRD_HYPERCH	8		/* Hyperchannel			*/
#define	ARPHRD_LANSTAR	9		/* Lanstar			*/
#define	ARPHRD_AUTONET	10		/* Autonet Short Address	*/
#define	ARPHRD_LOCALTLK	11		/* Localtalk			*/
#define	ARPHRD_LOCALNET	12		/* LocalNet (IBM PCNet/Sytek LocalNET) */
#define	ARPHRD_ULTRALNK	13		/* Ultra link			*/
#define	ARPHRD_SMDS	14		/* SMDS				*/
#define ARPHRD_DLCI	15		/* Frame Relay DLCI		*/
#define ARPHRD_ATM	16		/* ATM				*/
#define ARPHRD_HDLC	17		/* HDLC				*/
#define ARPHRD_FIBREC	18		/* Fibre Channel		*/
#define ARPHRD_ATM2225	19		/* ATM (RFC 2225)		*/
#define ARPHRD_SERIAL	20		/* Serial Line			*/
#define ARPHRD_ATM2	21		/* ATM				*/
#define ARPHRD_MS188220	22		/* MIL-STD-188-220		*/
#define ARPHRD_METRICOM	23		/* Metricom STRIP		*/
#define ARPHRD_IEEE1394	24		/* IEEE 1394.1995		*/
#define ARPHRD_MAPOS	25		/* MAPOS			*/
#define ARPHRD_TWINAX	26		/* Twinaxial			*/
#define ARPHRD_EUI_64	27		/* EUI-64			*/

/* Max string length for displaying unknown type of ARP address.  */
#define	MAX_ADDR_STR_LEN	16

static gchar *
arpaddr_to_str(guint8 *ad, int ad_len) {
  static gchar  str[3][MAX_ADDR_STR_LEN+3+1];
  static gchar *cur;
  gchar        *p;
  int           len;
  static const char hex[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

  if (cur == &str[0][0]) {
    cur = &str[1][0];
  } else if (cur == &str[1][0]) {  
    cur = &str[2][0];
  } else {  
    cur = &str[0][0];
  }
  p = cur;
  len = MAX_ADDR_STR_LEN;
  while (ad_len > 0 && len > 0) {
    *p++ = hex[(*ad) >> 4];
    *p++ = hex[(*ad) & 0xF];
    len -= 2;
    ad++;
    ad_len--;
  }
  if (ad_len != 0) {
    /* Note that we're not showing the full address.  */
    *p++ = '.';
    *p++ = '.';
    *p++ = '.';
  }
  *p = '\0';
  return cur;
}

static gchar *
arphrdaddr_to_str(guint8 *ad, int ad_len, guint16 type) {
  if (type == ARPHRD_ETHER && ad_len == 6) {
    /* Ethernet address.  */
    return ether_to_str(ad);
  }
  return arpaddr_to_str(ad, ad_len);
}

static gchar *
arpproaddr_to_str(guint8 *ad, int ad_len, guint16 type) {
  if (type == ETHERTYPE_IP && ad_len == 4) {
    /* IP address.  */
    return ip_to_str(ad);
  }
  return arpaddr_to_str(ad, ad_len);
}

/* Offsets of fields within an ARP packet. */
#define	AR_HRD		0
#define	AR_PRO		2
#define	AR_HLN		4
#define	AR_PLN		5
#define	AR_OP		6

void
dissect_arp(const u_char *pd, int offset, frame_data *fd, GtkTree *tree) {
  guint16     ar_hrd;
  guint16     ar_pro;
  guint8      ar_hln;
  guint8      ar_pln;
  guint16     ar_op;
  GtkWidget   *arp_tree, *ti;
  gchar       *op_str;
  int         sha_offset, spa_offset, tha_offset, tpa_offset;
  gchar       *sha_str, *spa_str, *tha_str, *tpa_str;
  static const value_string op_vals[] = {
    {ARPOP_REQUEST,  "ARP request" },
    {ARPOP_REPLY,    "ARP reply"   },
    {ARPOP_RREQUEST, "RARP request"},
    {ARPOP_RREPLY,   "RARP reply"  },
    {0,              NULL          } };
  static const value_string hrd_vals[] = {
    {ARPHRD_NETROM,   "NET/ROM pseudo"       },
    {ARPHRD_ETHER,    "Ethernet"             },
    {ARPHRD_EETHER,   "Experimental Ethernet"},
    {ARPHRD_AX25,     "AX.25"                },
    {ARPHRD_PRONET,   "ProNET"               },
    {ARPHRD_CHAOS,    "Chaos"                },
    {ARPHRD_IEEE802,  "IEEE 802"             },
    {ARPHRD_ARCNET,   "ARCNET"               },
    {ARPHRD_HYPERCH,  "Hyperchannel"         },
    {ARPHRD_LANSTAR,  "Lanstar"              },
    {ARPHRD_AUTONET,  "Autonet Short Address"},
    {ARPHRD_LOCALTLK, "Localtalk"            },
    {ARPHRD_LOCALNET, "LocalNet"             },
    {ARPHRD_ULTRALNK, "Ultra link"           },
    {ARPHRD_SMDS,     "SMDS"                 },
    {ARPHRD_DLCI,     "Frame Relay DLCI"     },
    {ARPHRD_ATM,      "ATM"                  },
    {ARPHRD_HDLC,     "HDLC"                 },
    {ARPHRD_FIBREC,   "Fibre Channel"        },
    {ARPHRD_ATM2225,  "ATM (RFC 2225)"       },
    {ARPHRD_SERIAL,   "Serial Line"          },
    {ARPHRD_ATM2,     "ATM"                  },
    {ARPHRD_MS188220, "MIL-STD-188-220"      },
    {ARPHRD_METRICOM, "Metricom STRIP"       },
    {ARPHRD_IEEE1394, "IEEE 1394.1995"       },
    {ARPHRD_MAPOS,    "MAPOS"                },
    {ARPHRD_TWINAX,   "Twinaxial"            },
    {ARPHRD_EUI_64,   "EUI-64"               },
    {0,                NULL                  } };

  /* To do: Check for {cap len,pkt len} < struct len */
  ar_hrd = pntohs(&pd[offset + AR_HRD]);
  ar_pro = pntohs(&pd[offset + AR_PRO]);
  ar_hln = (guint8) pd[offset + AR_HLN];
  ar_pln = (guint8) pd[offset + AR_PLN];
  ar_op  = pntohs(&pd[offset + AR_OP]);

  /* Extract the addresses.  */
  sha_offset = offset + 8;
  sha_str = arphrdaddr_to_str((guint8 *) &pd[sha_offset], ar_hln, ar_hrd);
  spa_offset = sha_offset + ar_hln;
  spa_str = arpproaddr_to_str((guint8 *) &pd[spa_offset], ar_pln, ar_pro);
  tha_offset = spa_offset + ar_pln;
  tha_str = arphrdaddr_to_str((guint8 *) &pd[tha_offset], ar_hln, ar_hrd);
  tpa_offset = tha_offset + ar_hln;
  tpa_str = arpproaddr_to_str((guint8 *) &pd[tpa_offset], ar_pln, ar_pro);
  
  if (check_col(fd, COL_PROTOCOL)) {
    if ((op_str = match_strval(ar_op, op_vals)))
      col_add_str(fd, COL_PROTOCOL, op_str);
    else
      col_add_str(fd, COL_PROTOCOL, "ARP");
  }

  if (check_col(fd, COL_INFO)) {
    switch (ar_op) {
      case ARPOP_REQUEST:
        col_add_fstr(fd, COL_INFO, "Who has %s?  Tell %s",
          tpa_str, spa_str);
        break;
      case ARPOP_REPLY:
        col_add_fstr(fd, COL_INFO, "%s is at %s", spa_str, sha_str);
        break;
      case ARPOP_RREQUEST:
        col_add_fstr(fd, COL_INFO, "Who is %s?  Tell %s",
          tha_str, sha_str);
        break;
      case ARPOP_RREPLY:
        col_add_fstr(fd, COL_INFO, "%s is at %s", sha_str, spa_str);
        break;
      default:
        col_add_fstr(fd, COL_INFO, "Unknown ARP opcode 0x%04x", ar_op);
        break;
    }
  }

  if (tree) {
    if ((op_str = match_strval(ar_op, op_vals)))
      ti = add_item_to_tree(GTK_WIDGET(tree), offset, 8 + 2*ar_hln + 2*ar_pln,
        op_str);
    else
      ti = add_item_to_tree(GTK_WIDGET(tree), offset, 8 + 2*ar_hln + 2*ar_pln,
        "Unknown ARP (opcode 0x%04x)", ar_op);
    arp_tree = gtk_tree_new();
    add_subtree(ti, arp_tree, ETT_ARP);
    add_item_to_tree(arp_tree, offset + AR_HRD, 2,
      "Hardware type: %s", val_to_str(ar_hrd, hrd_vals, "Unknown (0x%04x)"));
    add_item_to_tree(arp_tree, offset + AR_PRO, 2,
      "Protocol type: %s", ethertype_to_str(ar_pro, "Unknown (0x%04x)"));
    add_item_to_tree(arp_tree, offset + AR_HLN, 1,
      "Hardware size: %d", ar_hln);
    add_item_to_tree(arp_tree, offset + AR_PLN, 1,
      "Protocol size: %d", ar_pln);
    add_item_to_tree(arp_tree, offset + AR_OP,  2,
      "Opcode: 0x%04x (%s)", ar_op, op_str ? op_str : "Unknown");
    add_item_to_tree(arp_tree, sha_offset, ar_hln,
      "Sender hardware address: %s", sha_str);
    add_item_to_tree(arp_tree, spa_offset, ar_pln,
      "Sender protocol address: %s", spa_str);
    add_item_to_tree(arp_tree, tha_offset, ar_hln,
      "Target hardware address: %s", tha_str);
    add_item_to_tree(arp_tree, tpa_offset, ar_pln,
      "Target protocol address: %s", tpa_str);
  }
}
