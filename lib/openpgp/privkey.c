/*
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Free Software Foundation
 *
 * Author: Nikos Mavrogiannopoulos
 *
 * This file is part of GNUTLS-EXTRA.
 *
 * GNUTLS-EXTRA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *               
 * GNUTLS-EXTRA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *                               
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Functions on OpenPGP privkey parsing
 */

#include <gnutls_int.h>
#include <gnutls_datum.h>
#include <gnutls_global.h>
#include <gnutls_errors.h>
#include <gnutls_num.h>
#include <openpgp.h>
#include <gnutls_openpgp.h>
#include <x509/rfc2818.h>
#include <gnutls_cert.h>

/**
 * gnutls_openpgp_privkey_init - This function initializes a gnutls_openpgp_privkey_t structure
 * @key: The structure to be initialized
 *
 * This function will initialize an OpenPGP key structure. 
 *
 * Returns 0 on success.
 *
 **/
int
gnutls_openpgp_privkey_init (gnutls_openpgp_privkey_t * key)
{
  *key = gnutls_calloc (1, sizeof (gnutls_openpgp_privkey_int));

  if (*key)
    return 0; /* success */
  return GNUTLS_E_MEMORY_ERROR;
}

/**
 * gnutls_openpgp_privkey_deinit - This function deinitializes memory used by a gnutls_openpgp_privkey_t structure
 * @key: The structure to be initialized
 *
 * This function will deinitialize a key structure. 
 *
 **/
void
gnutls_openpgp_privkey_deinit (gnutls_openpgp_privkey_t key)
{
  if (!key)
    return;

  if (key->knode)
    {
      cdk_kbnode_release (key->knode);
      key->knode = NULL;
    }
  
  gnutls_free (key);
}

/**
 * gnutls_openpgp_privkey_import - This function will import a RAW or BASE64 encoded key
 * @key: The structure to store the parsed key.
 * @data: The RAW or BASE64 encoded key.
 * @format: One of gnutls_openpgp_crt_fmt_t elements.
 * @pass: Unused for now
 * @flags: should be zero
 *
 * This function will convert the given RAW or Base64 encoded key
 * to the native gnutls_openpgp_privkey_t format. The output will be stored in 'key'.
 *
 * Returns 0 on success.
 *
 **/
int
gnutls_openpgp_privkey_import (gnutls_openpgp_privkey_t key,
			       const gnutls_datum_t * data,
			       gnutls_openpgp_crt_fmt_t format,
			       const char *pass, unsigned int flags)
{
  cdk_stream_t inp;  
  int rc;
  
  if (format == GNUTLS_OPENPGP_FMT_RAW)
    rc = cdk_kbnode_read_from_mem (&key->knode, data->data, data->size);
  else
    {
      rc = cdk_stream_tmp_from_mem (data->data, data->size, &inp);
      if (rc)
	{
	  rc = _gnutls_map_cdk_rc (rc);
	  gnutls_assert ();
	  return rc;
	}      
      if (cdk_armor_filter_use (inp))
	rc = cdk_stream_set_armor_flag (inp, 0);
      if (!rc)
	rc = cdk_keydb_get_keyblock (inp, &key->knode);
      cdk_stream_close (inp);
      if (rc)
	{
	  rc = _gnutls_map_cdk_rc (rc);
	  gnutls_assert ();
	  return rc;
	}
    }
  
  return 0;
}

/**
  * gnutls_openpgp_privkey_export - This function will export a RAW or BASE64 encoded key
  * @key: Holds the key.
  * @format: One of gnutls_openpgp_crt_fmt_t elements.
  * @password: the password that will be used to encrypt the key. 
  * @flags: zero for future compatibility
  * @output_data: will contain the key base64 encoded or raw
  * @output_data_size: holds the size of output_data (and will be replaced by the actual size of parameters)
  *
  * This function will convert the given key to RAW or Base64 format.
  * If the buffer provided is not long enough to hold the output, then
  * GNUTLS_E_SHORT_MEMORY_BUFFER will be returned.
  *
  * Returns 0 on success.
  *
  **/
int
gnutls_openpgp_privkey_export (gnutls_openpgp_privkey_t key,
			   gnutls_openpgp_crt_fmt_t format,
			   const char* password, unsigned int flags,
			   void *output_data, size_t * output_data_size)
{
  /* FIXME for now we do not export encrypted keys */
  return _gnutls_openpgp_export( key->knode, format, output_data, output_data_size);
}


/**
 * gnutls_openpgp_privkey_get_pk_algorithm - This function returns the key's PublicKey algorithm
 * @key: is an OpenPGP key
 * @bits: if bits is non null it will hold the size of the parameters' in bits
 *
 * This function will return the public key algorithm of an OpenPGP
 * certificate.
 *
 * If bits is non null, it should have enough size to hold the parameters
 * size in bits. For RSA the bits returned is the modulus. 
 * For DSA the bits returned are of the public exponent.
 *
 * Returns a member of the GNUTLS_PKAlgorithm enumeration on success,
 * or a negative value on error.
 *
 **/
gnutls_pk_algorithm_t
gnutls_openpgp_privkey_get_pk_algorithm (gnutls_openpgp_privkey_t key,
					 unsigned int *bits)
{
  cdk_packet_t pkt;
  int algo;

  if (!key)
    return GNUTLS_PK_UNKNOWN;
  
  algo = 0;
  pkt = cdk_kbnode_find_packet (key->knode, CDK_PKT_SECRET_KEY);
  if (pkt)
    {
      if (bits)
	*bits = cdk_pk_get_nbits (pkt->pkt.secret_key->pk);
      algo = _gnutls_openpgp_get_algo(pkt->pkt.secret_key->pk->pubkey_algo);
    }
  
  return algo;
}

int _gnutls_openpgp_get_algo( int cdk_algo)
{
int algo;

      if (is_RSA (cdk_algo))
	algo = GNUTLS_PK_RSA;
      else if (is_DSA (cdk_algo))
	algo = GNUTLS_PK_DSA;
      else
	algo = GNUTLS_PK_UNKNOWN;
      
      return algo;
}

/**
 * gnutls_openpgp_privkey_get_revoked_ status - Gets the revoked status of the key
 * @key: the structure that contains the OpenPGP private key.
 *
 * Returns the true (1) or false (0) based on whether this key has been revoked
 * or not. A negative value indicates an error.
 *
 **/
int
gnutls_openpgp_privkey_get_revoked_status (gnutls_openpgp_privkey_t key)
{
  cdk_packet_t pkt;

  if (!key)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  pkt = cdk_kbnode_find_packet (key->knode, CDK_PKT_SECRET_KEY);
  if (!pkt)
    return GNUTLS_E_OPENPGP_GETKEY_FAILED;

  if (pkt->pkt.secret_key->is_revoked != 0) return 1;
  return 0;
}

/**
 * gnutls_openpgp_privkey_get_fingerprint - Gets the fingerprint
 * @key: the raw data that contains the OpenPGP secret key.
 * @fpr: the buffer to save the fingerprint, must hold at least 20 bytes.
 * @fprlen: the integer to save the length of the fingerprint.
 *
 * Returns the fingerprint of the OpenPGP key. Depends on the algorithm,
 * the fingerprint can be 16 or 20 bytes.
 **/
int
gnutls_openpgp_privkey_get_fingerprint (gnutls_openpgp_privkey_t key,
				    void *fpr, size_t * fprlen)
{
  cdk_packet_t pkt;
  cdk_pkt_pubkey_t pk = NULL;

  if (!fpr || !fprlen)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  *fprlen = 0;

  pkt = cdk_kbnode_find_packet (key->knode, CDK_PKT_SECRET_KEY);
  if (!pkt) 
    {
      gnutls_assert();
      return GNUTLS_E_OPENPGP_GETKEY_FAILED;
    }

  pk = pkt->pkt.secret_key->pk;
  *fprlen = 20;
  
  if (is_RSA (pk->pubkey_algo) && pk->version < 4)
    *fprlen = 16;

  cdk_pk_get_fingerprint (pk, fpr);

  return 0;
}

/**
 * gnutls_openpgp_privkey_get_key_id - Gets the keyID
 * @key: the structure that contains the OpenPGP secret key.
 * @keyid: the buffer to save the keyid.
 *
 * Returns the 64-bit keyID of the OpenPGP key.
 **/
int
gnutls_openpgp_privkey_get_key_id (gnutls_openpgp_privkey_t key, gnutls_openpgp_keyid_t keyid)
{
  cdk_packet_t pkt;
  uint32_t kid[2];

  if (!key || !keyid)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }
 
  pkt = cdk_kbnode_find_packet (key->knode, CDK_PKT_SECRET_KEY);
  if (!pkt)
    return GNUTLS_E_OPENPGP_GETKEY_FAILED;

  cdk_sk_get_keyid (pkt->pkt.secret_key, kid);
  _gnutls_write_uint32( kid[0], keyid);
  _gnutls_write_uint32( kid[1], keyid+4);

  return 0;
}


/**
  * gnutls_openpgp_privkey_get_subkey_count - This function returns the number of subkeys
  * @key: is an OpenPGP key
  *
  * This function will return the number of subkeys present in the given 
  * OpenPGP certificate.
  *
  * Returns then number of subkeys or a negative value on error.
  *
  **/
int
gnutls_openpgp_privkey_get_subkey_count (gnutls_openpgp_privkey_t key)
{
  cdk_kbnode_t p, ctx;
  cdk_packet_t pkt;
  int subkeys;

  if (key == NULL)
    {
      gnutls_assert ();
      return 0;
    }
  
  ctx = NULL;
  subkeys = 0;
  while ((p = cdk_kbnode_walk (key->knode, &ctx, 0)))
    {
      pkt = cdk_kbnode_get_packet (p);
      if (pkt->pkttype == CDK_PKT_SECRET_SUBKEY)
	subkeys++;
    }

  return subkeys;
}

/* returns the subkey with the given index */
static cdk_packet_t _get_secret_subkey(gnutls_openpgp_privkey_t key, unsigned int indx)
{
  cdk_kbnode_t p, ctx;
  cdk_packet_t pkt;
  int subkeys;

  ctx = NULL;
  subkeys = 0;
  while ((p = cdk_kbnode_walk (key->knode, &ctx, 0)))
    {
      pkt = cdk_kbnode_get_packet (p);
      if (pkt->pkttype == CDK_PKT_SECRET_SUBKEY && indx == subkeys++)
	return pkt;
    }

  return NULL;
}

/**
 * gnutls_openpgp_privkey_get_subkey_revoked_ status - Gets the revoked status of the key
 * @key: the structure that contains the OpenPGP private key.
 * @idx: is the subkey index
 *
 * Returns the true (1) or false (0) based on whether this key has been revoked
 * or not. A negative value indicates an error.
 *
 **/
int
gnutls_openpgp_privkey_get_subkey_revoked_status (gnutls_openpgp_privkey_t key, unsigned int idx)
{
  cdk_packet_t pkt;

  if (!key)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  pkt = _get_secret_subkey( key, idx);
  if (!pkt)
    return GNUTLS_E_OPENPGP_GETKEY_FAILED;

  if (pkt->pkt.secret_key->is_revoked != 0) return 1;
  return 0;
}

/**
  * gnutls_openpgp_privkey_get_subkey_pk_algorithm - This function returns the subkey's PublicKey algorithm
  * @key: is an OpenPGP key
  * @idx: is the subkey index
  * @bits: if bits is non null it will hold the size of the parameters' in bits
  *
  * This function will return the public key algorithm of a subkey of an OpenPGP
  * certificate.
  *
  * If bits is non null, it should have enough size to hold the parameters
  * size in bits. For RSA the bits returned is the modulus. 
  * For DSA the bits returned are of the public exponent.
  *
  * Returns a member of the gnutls_pk_algorithm_t enumeration on success,
  * or a negative value on error.
  *
  **/
gnutls_pk_algorithm_t
gnutls_openpgp_privkey_get_subkey_pk_algorithm (gnutls_openpgp_privkey_t key,
    unsigned int idx, unsigned int *bits)
{
  cdk_packet_t pkt;
  int algo;

  if (!key)
    return GNUTLS_PK_UNKNOWN;
  
  pkt = _get_secret_subkey( key, idx);

  algo = 0;
  if (pkt)
    {
      if (bits)
	*bits = cdk_pk_get_nbits (pkt->pkt.secret_key->pk);
      algo = pkt->pkt.secret_key->pubkey_algo;
      if (is_RSA (algo))
	algo = GNUTLS_PK_RSA;
      else if (is_DSA (algo))
	algo = GNUTLS_PK_DSA;
      else
	algo = GNUTLS_E_UNKNOWN_PK_ALGORITHM;
    }
  
  return algo;
}

/**
 * gnutls_openpgp_privkey_get_subkey_idx - Returns the subkey's index
 * @key: the structure that contains the OpenPGP private key.
 * @keyid: the keyid.
 *
 * Returns the index of the subkey or a negative error value.
 *
 **/
int
gnutls_openpgp_privkey_get_subkey_idx (gnutls_openpgp_privkey_t key, const gnutls_openpgp_keyid_t keyid)
{
  cdk_packet_t pkt;
  int ret;
  uint32_t kid[2];

  if (!key)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  KEYID_IMPORT( kid, keyid);
  ret = _gnutls_openpgp_find_subkey_idx( key->knode, kid, 1);

  if (ret < 0)
    {
      gnutls_assert();
    }

  return ret;
}

/**
 * gnutls_openpgp_privkey_get_subkey_creation_time - Extract the timestamp
 * @key: the structure that contains the OpenPGP private key.
 * @idx: the subkey index
 *
 * Returns the timestamp when the OpenPGP key was created.
 **/
time_t
gnutls_openpgp_privkey_get_subkey_creation_time (gnutls_openpgp_privkey_t key, unsigned int idx)
{
  cdk_packet_t pkt;
  time_t timestamp;

  if (!key)
    return (time_t) - 1;
    
  pkt = _get_secret_subkey( key, idx);
  if (pkt)
    timestamp = pkt->pkt.secret_key->pk->timestamp;
  else
    timestamp = 0;

  return timestamp;
}

/**
 * gnutls_openpgp_privkey_get_subkey_expiration_time - Extract the expire date
 * @key: the structure that contains the OpenPGP private key.
 * @idx: the subkey index
 *
 * Returns the time when the OpenPGP key expires. A value of '0' means
 * that the key doesn't expire at all.
 **/
time_t
gnutls_openpgp_privkey_get_subkey_expiration_time (gnutls_openpgp_privkey_t key, unsigned int idx)
{
  cdk_packet_t pkt;
  time_t expiredate;

  if (!key)
    return (time_t) - 1;

  pkt = _get_secret_subkey( key, idx);
  if (pkt)
    expiredate = pkt->pkt.secret_key->expiredate;
  else
    expiredate = 0;

  return expiredate;
}

/**
 * gnutls_openpgp_privkey_get_subkey_id - Gets the keyID
 * @key: the structure that contains the OpenPGP secret key.
 * @idx: the subkey index
 * @keyid: the buffer to save the keyid.
 *
 * Returns the 64-bit keyID of the OpenPGP key.
 **/
int
gnutls_openpgp_privkey_get_subkey_id (gnutls_openpgp_privkey_t key, unsigned int idx, gnutls_openpgp_keyid_t keyid)
{
  cdk_packet_t pkt;
  uint32_t kid[2];

  if (!key || !keyid)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }
 
  pkt = _get_secret_subkey( key, idx);
  if (!pkt)
    return GNUTLS_E_OPENPGP_GETKEY_FAILED;

  cdk_sk_get_keyid (pkt->pkt.secret_key, kid);
  _gnutls_write_uint32( kid[0], keyid);
  _gnutls_write_uint32( kid[1], keyid+4);

  return 0;
}

/* Extracts DSA and RSA parameters from a certificate.
 */
int
_gnutls_openpgp_privkey_get_mpis (gnutls_openpgp_privkey_t pkey, uint32_t *keyid /*[2]*/,
			   mpi_t * params, int *params_size)
{
  int result, i;
  int pk_algorithm, local_params;
  cdk_packet_t pkt;

  if (keyid == NULL)
    pkt = cdk_kbnode_find_packet (pkey->knode, CDK_PKT_SECRET_KEY);
  else
    pkt = _gnutls_openpgp_find_key( pkey->knode, keyid, 1);
  
  if (pkt == NULL)
    {
      gnutls_assert();
      return GNUTLS_E_OPENPGP_GETKEY_FAILED;
    }

  pk_algorithm = _gnutls_openpgp_get_algo( pkt->pkt.secret_key->pk->pubkey_algo);

  switch (pk_algorithm)
    {
      case GNUTLS_PK_RSA:
        local_params = RSA_PRIVATE_PARAMS;
        break;
      case GNUTLS_PK_DSA:
        local_params = DSA_PRIVATE_PARAMS;
        break;
      default:
        gnutls_assert ();
        return GNUTLS_E_UNSUPPORTED_CERTIFICATE_TYPE;
    }

  if (*params_size < local_params) 
    {
      gnutls_assert();
      return GNUTLS_E_INTERNAL_ERROR;
    }
    
  *params_size = local_params;

    
  for (i = 0; i < local_params; i++)
    {
       result = _gnutls_read_pgp_mpi( pkt, 1, i, &params[i]);
       if (result < 0)
         {
           gnutls_assert();
           goto error;
         }
    }

  return 0;
  
error:
  {
    int j;
    for (j=0;j<i;j++)
      _gnutls_mpi_release( &params[j]);
  }

  return result;
}

/* The internal version of export
 */
static
int _get_sk_rsa_raw(gnutls_openpgp_privkey_t pkey, gnutls_openpgp_keyid_t keyid,
  gnutls_datum_t * m, gnutls_datum_t * e,
  gnutls_datum_t * d, gnutls_datum_t * p,
  gnutls_datum_t * q, gnutls_datum_t * u)
{
  int pk_algorithm, ret, i;
  cdk_packet_t pkt;
  uint32_t kid32[2];
  mpi_t params[MAX_PRIV_PARAMS_SIZE];
  int params_size = MAX_PRIV_PARAMS_SIZE;

  if (pkey == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }
  
  KEYID_IMPORT(kid32, keyid);
  
  pkt = _gnutls_openpgp_find_key( pkey->knode, kid32, 1);
  if (pkt == NULL)
    {
      gnutls_assert();
      return GNUTLS_E_OPENPGP_GETKEY_FAILED;
    }

  pk_algorithm = _gnutls_openpgp_get_algo( pkt->pkt.secret_key->pk->pubkey_algo);
  
  if (pk_algorithm != GNUTLS_PK_RSA)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  ret = _gnutls_openpgp_privkey_get_mpis (pkey, kid32, params, &params_size);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  ret = _gnutls_mpi_dprint (m, params[0]);
  if (ret < 0)
    {
      gnutls_assert ();
      goto cleanup;
    }

  ret = _gnutls_mpi_dprint (e, params[1]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (m);
      goto cleanup;
    }

  ret = _gnutls_mpi_dprint (d, params[2]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (m);
      _gnutls_free_datum (e);
      goto cleanup;
    }

  ret = _gnutls_mpi_dprint (p, params[3]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (m);
      _gnutls_free_datum (e);
      _gnutls_free_datum (d);
      goto cleanup;
    }

  ret = _gnutls_mpi_dprint (q, params[4]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (m);
      _gnutls_free_datum (e);
      _gnutls_free_datum (d);
      _gnutls_free_datum (p);
      goto cleanup;
    }

  ret = _gnutls_mpi_dprint (u, params[5]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (q);
      _gnutls_free_datum (m);
      _gnutls_free_datum (e);
      _gnutls_free_datum (d);
      _gnutls_free_datum (p);
      goto cleanup;
    }

  ret = 0;

cleanup:
  for (i = 0; i < params_size; i++)
    {
      _gnutls_mpi_release (&params[i]);
    }
  return ret;
}

static
int _get_sk_dsa_raw(gnutls_openpgp_privkey_t pkey, gnutls_openpgp_keyid_t keyid,
				gnutls_datum_t * p, gnutls_datum_t * q,
				gnutls_datum_t * g, gnutls_datum_t * y,
				gnutls_datum_t * x)
{
  int pk_algorithm, ret, i;
  cdk_packet_t pkt;
  uint32_t kid32[2];
  mpi_t params[MAX_PRIV_PARAMS_SIZE];
  int params_size = MAX_PRIV_PARAMS_SIZE;

  if (pkey == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }
  
  KEYID_IMPORT(kid32, keyid);
  
  pkt = _gnutls_openpgp_find_key( pkey->knode, kid32, 0);
  if (pkt == NULL)
    {
      gnutls_assert();
      return GNUTLS_E_OPENPGP_GETKEY_FAILED;
    }

  pk_algorithm = _gnutls_openpgp_get_algo( pkt->pkt.secret_key->pk->pubkey_algo);
  
  if (pk_algorithm != GNUTLS_PK_DSA)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  ret = _gnutls_openpgp_privkey_get_mpis (pkey, kid32, params, &params_size);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  /* P */
  ret = _gnutls_mpi_dprint (p, params[0]);
  if (ret < 0)
    {
      gnutls_assert ();
      goto cleanup;
    }

  /* Q */
  ret = _gnutls_mpi_dprint (q, params[1]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (p);
      goto cleanup;
    }


  /* G */
  ret = _gnutls_mpi_dprint (g, params[2]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (p);
      _gnutls_free_datum (q);
      goto cleanup;
    }


  /* Y */
  ret = _gnutls_mpi_dprint (y, params[3]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (p);
      _gnutls_free_datum (g);
      _gnutls_free_datum (q);
      goto cleanup;
    }

  ret = _gnutls_mpi_dprint (x, params[4]);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_free_datum (y);
      _gnutls_free_datum (p);
      _gnutls_free_datum (g);
      _gnutls_free_datum (q);
      goto cleanup;
    }

  ret = 0;

cleanup:
  for (i = 0; i < params_size; i++)
    {
      _gnutls_mpi_release (&params[i]);
    }
  return ret;
}


/**
  * gnutls_openpgp_privkey_export_rsa_raw - This function will export the RSA private key
  * @pkey: Holds the certificate
  * @m: will hold the modulus
  * @e: will hold the public exponent
  * @d: will hold the private exponent
  * @p: will hold the first prime (p)
  * @q: will hold the second prime (q)
  * @u: will hold the coefficient
  *
  * This function will export the RSA private key's parameters found in
  * the given structure.  The new parameters will be allocated using
  * gnutls_malloc() and will be stored in the appropriate datum.
  *
  * Returns: %GNUTLS_E_SUCCESS on success, otherwise an error.
  **/
int
gnutls_openpgp_privkey_export_rsa_raw (gnutls_openpgp_privkey_t pkey, 
				    gnutls_datum_t * m, gnutls_datum_t * e,
				    gnutls_datum_t * d, gnutls_datum_t * p,
				    gnutls_datum_t * q, gnutls_datum_t * u)
{
gnutls_openpgp_keyid_t keyid;
int ret;

  ret = gnutls_openpgp_privkey_get_key_id( pkey, keyid);  
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }
    
  return _get_sk_rsa_raw( pkey, keyid, m, e, d, p, q, u);
}

/**
  * gnutls_openpgp_privkey_export_dsa_raw - This function will export the DSA private key
  * @pkey: Holds the certificate
  * @p: will hold the p
  * @q: will hold the q
  * @g: will hold the g
  * @y: will hold the y
  * @x: will hold the x
  *
  * This function will export the DSA private key's parameters found in
  * the given certificate.  The new parameters will be allocated using
  * gnutls_malloc() and will be stored in the appropriate datum.
  *
  * Returns: %GNUTLS_E_SUCCESS on success, otherwise an error.
  **/
int
gnutls_openpgp_privkey_export_dsa_raw (gnutls_openpgp_privkey_t pkey, 
				    gnutls_datum_t * p, gnutls_datum_t * q,
				    gnutls_datum_t * g, gnutls_datum_t * y,
				    gnutls_datum_t * x)
{
gnutls_openpgp_keyid_t keyid;
int ret;

  ret = gnutls_openpgp_privkey_get_key_id( pkey, keyid);  
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }
    
  return _get_sk_dsa_raw( pkey, keyid, p, q, g, y, x);
}

/**
  * gnutls_openpgp_privkey_export_subkey_rsa_raw - This function will export the RSA private key
  * @pkey: Holds the certificate
  * @idx: Is the subkey index
  * @m: will hold the modulus
  * @e: will hold the public exponent
  * @d: will hold the private exponent
  * @p: will hold the first prime (p)
  * @q: will hold the second prime (q)
  * @u: will hold the coefficient
  *
  * This function will export the RSA private key's parameters found in
  * the given structure.  The new parameters will be allocated using
  * gnutls_malloc() and will be stored in the appropriate datum.
  *
  * Returns: %GNUTLS_E_SUCCESS on success, otherwise an error.
  **/
int
gnutls_openpgp_privkey_export_subkey_rsa_raw (gnutls_openpgp_privkey_t pkey, unsigned int idx,
				    gnutls_datum_t * m, gnutls_datum_t * e,
				    gnutls_datum_t * d, gnutls_datum_t * p,
				    gnutls_datum_t * q, gnutls_datum_t * u)
{
gnutls_openpgp_keyid_t keyid;
int ret;

  ret = gnutls_openpgp_privkey_get_subkey_id( pkey, idx, keyid);  
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }
    
  return _get_sk_rsa_raw( pkey, keyid, m, e, d, p, q, u);
}

/**
  * gnutls_openpgp_privkey_export_subkey_dsa_raw - This function will export the DSA private key
  * @pkey: Holds the certificate
  * @idx: Is the subkey index
  * @p: will hold the p
  * @q: will hold the q
  * @g: will hold the g
  * @y: will hold the y
  * @x: will hold the x
  *
  * This function will export the DSA private key's parameters found in
  * the given certificate.  The new parameters will be allocated using
  * gnutls_malloc() and will be stored in the appropriate datum.
  *
  * Returns: %GNUTLS_E_SUCCESS on success, otherwise an error.
  **/
int
gnutls_openpgp_privkey_export_subkey_dsa_raw (gnutls_openpgp_privkey_t pkey, unsigned int idx,
				    gnutls_datum_t * p, gnutls_datum_t * q,
				    gnutls_datum_t * g, gnutls_datum_t * y,
				    gnutls_datum_t * x)
{
gnutls_openpgp_keyid_t keyid;
int ret;

  ret = gnutls_openpgp_privkey_get_subkey_id( pkey, idx, keyid);  
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }
    
  return _get_sk_dsa_raw( pkey, keyid, p, q, g, y, x);
}

/**
 * gnutls_openpgp_privkey_get_preferred_key_id - Gets the preferred keyID
 * @key: the structure that contains the OpenPGP public key.
 * @keyid: the struct to save the keyid.
 *
 * Returns the 64-bit preferred keyID of the OpenPGP key. If it hasn't
 * been set it returns GNUTLS_E_INVALID_REQUEST.
 **/
int
gnutls_openpgp_privkey_get_preferred_key_id (gnutls_openpgp_privkey_t key, gnutls_openpgp_keyid_t keyid)
{
  if (!key || !keyid || !key->preferred_set)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  memcpy( keyid, key->preferred_keyid, sizeof(keyid));

  return 0;
}

/**
 * gnutls_openpgp_privkey_set_preferred_key_id - Sets the prefered keyID
 * @key: the structure that contains the OpenPGP public key.
 * @keyid: the selected keyid
 *
 * This allows setting a preferred key id for the given certificate.
 * This key will be used by functions that involve key handling.
 *
 **/
int
gnutls_openpgp_privkey_set_preferred_key_id (gnutls_openpgp_privkey_t key, 
  const gnutls_openpgp_keyid_t keyid)
{
int ret;

  if (!key)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  /* check if the id is valid */
  ret = gnutls_openpgp_privkey_get_subkey_idx ( key, keyid);
  if (ret < 0)
    {
      _gnutls_x509_log("the requested subkey does not exist\n");
      gnutls_assert();
      return ret;
    }

  key->preferred_set = 1;
  memcpy( key->preferred_keyid, keyid, sizeof(keyid));

  return 0;
}
