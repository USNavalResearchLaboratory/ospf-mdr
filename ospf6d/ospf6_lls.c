/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2011 The Boeing Company
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "zebra.h"
#include "checksum.h"
#include "log.h"

#include "ospf6_message.h"
#include "ospf6_proto.h"
#include "ospf6_lls.h"

/**
 * Check if the L-bit is set.
 *
 * @param oh A pointer to the OSPF packet header
 *
 * @return Nonzero if the L-bit is set indicating the packet contains
 * a LLS data block; zero otherwise.
 */
int
ospf6_lls_option_isset (struct ospf6_header *oh)
{
  int lls = 0;

  switch (oh->type)
    {
    case OSPF6_MESSAGE_TYPE_HELLO:
      {
	struct ospf6_hello *hello = (void *)(oh + 1);
	if (OSPF6_OPT_ISSET (hello->options, OSPF6_OPT_L, 1))
	  lls = 1;
      }
      break;

    case OSPF6_MESSAGE_TYPE_DBDESC:
      {
	struct ospf6_dbdesc *dbdesc = (void *)(oh + 1);
	if (OSPF6_OPT_ISSET (dbdesc->options, OSPF6_OPT_L, 1))
	  lls = 1;
      }
      break;

    default:
      break;
    }

  return lls;
}

/**
 * Clear the L-bit to indicate a packet does not contain LLS data.
 *
 * @param oh A pointer to the OSPF packet header
 */
void
ospf6_lls_option_clear (struct ospf6_header *oh)
{
  switch (oh->type)
    {
    case OSPF6_MESSAGE_TYPE_HELLO:
      {
	struct ospf6_hello *hello = (void *)(oh + 1);
	OSPF6_OPT_CLEAR (hello->options, OSPF6_OPT_L, 1);
      }
      break;

    case OSPF6_MESSAGE_TYPE_DBDESC:
      {
	struct ospf6_dbdesc *dbdesc = (void *)(oh + 1);
	OSPF6_OPT_CLEAR (dbdesc->options, OSPF6_OPT_L, 1);
      }
      break;

    default:
      break;
    }
}

/**
 * Fill in the LLS header.
 *
 * Set the length appropriately and calculate the checksum.
 *
 * @param lls A pointer to the beginning of LLS data
 * @param len Total LLS data length in bytes
 */
void
ospf6_set_lls_header (struct ospf6_lls_header *lls, size_t len)
{
  int cksum;

  /*
   * RFC 5613 2.2:
   *
   * All TLVs MUST be 32-bit aligned (with padding if necessary).
   */
  assert ((len & 0x3) == 0);

  /*
   * RFC 5613 2.2:
   *
   * The 16-bit LLS Data Length field contains the length (in 32-bit
   * words) of the LLS block including the header and payload.
   */
  assert ((len >> 2) <= 0xffff);
  lls->datalen = htons ((uint16_t) (len >> 2));

  /*
   * RFC 5613 2.2:
   *
   * The Checksum field contains the standard IP checksum for the entire
   * contents of the LLS block.  Before computing the checksum, the
   * checksum field is set to 0.
   */
  lls->cksum = 0;
  cksum = in_cksum (lls, len);
  lls->cksum = cksum & 0xffff;
}

/**
 * Check that a LLS data block is valid
 *
 * A LLS data block is considered valid if all length fields are
 * consistent and the checksum is correct.
 *
 * @param lls A pointer to the beginning of LLS data
 * @param len Total LLS data length in bytes
 *
 * @return Zero if the entire LLS data block is valid; nonzero if an
 *         error or inconsistency was found.
 */
int
ospf6_lls_validate_datablock (struct ospf6_lls_header *lls,
			      size_t len, int debug)
{
  size_t datalen;
  int cksum;
  struct ospf6_tlv_header *tlv, *end;

  if (len < sizeof (*lls))
    {
      if (debug)
	zlog_debug ("%s: incomplete LLS header", __func__);
      return -1;
    }

  /* check LLS header length field */
  datalen = ntohs (lls->datalen) << 2;
  if (len < datalen)
    {
      if (debug)
	zlog_debug ("%s: insufficient LLS data: %zu < %zu",
		    __func__, len, datalen);
      return -1;
    }

  if (len > datalen)
    zlog_warn ("%s: ignoring trailing %zu bytes of message data",
	       __func__, len - datalen);

  /* check the checksum */
  cksum = in_cksum (lls, datalen);
  if (cksum != 0)
    {
      if (debug)
	zlog_debug ("%s: incorrect LLS checksum: 0x%04x",
		    __func__, ntohs (lls->cksum));
      return -1;
    }

  /* check that the length of individual TLVs is consistent */
  tlv = (struct ospf6_tlv_header *) (lls + 1);
  end = (struct ospf6_tlv_header *) ((char *) lls + datalen);
  while (tlv < end)
    {
      uint16_t vallen, pad;

      vallen = ntohs (tlv->vallen);

      pad = vallen & 0x3;
      if (pad)
	pad = 4 - pad;

      tlv = (struct ospf6_tlv_header *) ((char *) (tlv + 1) + vallen + pad);
    }

  if (tlv != end)
    {
      if (debug)
	zlog_debug ("%s: LLS TLV total length inconsistent", __func__);
      return -1;
    }

  return 0;
}
