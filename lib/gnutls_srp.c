/*
 *      Copyright (C) 2001 Nikos Mavroyanopoulos
 *
 * This file is part of GNUTLS.
 *
 * GNUTLS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GNUTLS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <gnutls_int.h>
#include <gnutls_errors.h>
#include <crypt_bcrypt.h>
#include <gnutls_srp.h>
#include <auth_srp_passwd.h>
#include <auth_srp.h>
#include <gnutls_gcry.h>
#include "debug.h"


/* Here functions for SRP (like g^x mod n) are defined 
 */


int _gnutls_srp_gx(opaque * text, int textsize, opaque ** result, MPI g,
		   MPI prime)
{

	MPI x, e;
	int result_size;

	if (gcry_mpi_scan(&x, GCRYMPI_FMT_USG, text, &textsize)) {
		gnutls_assert();
		return GNUTLS_E_MPI_SCAN_FAILED;
	}

	e = gcry_mpi_alloc_like(prime);
	if (e==NULL) {
		gnutls_assert();
		_gnutls_mpi_release(&x);
		return GNUTLS_E_MEMORY_ERROR;
	}

	/* e = g^x mod prime (n) */
	gcry_mpi_powm(e, g, x, prime);
	_gnutls_mpi_release(&x);

	gcry_mpi_print(GCRYMPI_FMT_USG, NULL, &result_size, e);
	if (result != NULL) {
		*result = gnutls_malloc(result_size);
		if ((*result)==NULL) return GNUTLS_E_MEMORY_ERROR;

		gcry_mpi_print(GCRYMPI_FMT_USG, *result, &result_size, e);
	}

	_gnutls_mpi_release(&e);

	return result_size;

}


/****************
 * Choose a random value b and calculate B = (v + g^b) % N.
 * Return: B and if ret_b is not NULL b.
 */
MPI _gnutls_calc_srp_B(MPI * ret_b, MPI g, MPI n, MPI v)
{
	MPI tmpB;
	MPI b, B;
	int bits;

	/* calculate:  B = (v + g^b) % N */
	bits = gcry_mpi_get_nbits(n);
	b = gcry_mpi_new(bits);	/* FIXME: allocate in secure memory */
	if (b==NULL) {
		gnutls_assert();
		return NULL;
	}
	
	gcry_mpi_randomize(b, bits, GCRY_STRONG_RANDOM);

	tmpB = gcry_mpi_new(bits);	/* FIXME: allocate in secure memory */
	if (tmpB==NULL) {
		gnutls_assert();
		_gnutls_mpi_release( &b);
		return NULL;
	}

	B = gcry_mpi_new(bits);	/* FIXME: allocate in secure memory */
	if (tmpB==NULL) {
		gnutls_assert();
		_gnutls_mpi_release( &b);
		_gnutls_mpi_release( &tmpB);
		return NULL;
	}

	gcry_mpi_powm(tmpB, g, b, n);
	gcry_mpi_addm(B, v, tmpB, n);

	_gnutls_mpi_release(&tmpB);

	if (ret_b)
		*ret_b = b;
	else
		_gnutls_mpi_release(&b);

	return B;
}

MPI _gnutls_calc_srp_u(MPI B)
{
	int b_size;
	opaque *b_holder, hd[MAX_HASH_SIZE];
	GNUTLS_MAC_HANDLE td;
	uint32 u;
	MPI ret;

	gcry_mpi_print(GCRYMPI_FMT_USG, NULL, &b_size, B);
	b_holder = gnutls_malloc(b_size);
	if (b_holder==NULL) return NULL;

	gcry_mpi_print(GCRYMPI_FMT_USG, b_holder, &b_size, B);


	td = gnutls_hash_init(GNUTLS_MAC_SHA);
	if (td==NULL) {
		gnutls_free(b_holder);
		gnutls_assert();
		return NULL;
	}
	gnutls_hash(td, b_holder, b_size);
	gnutls_hash_deinit(td, hd);
	
	memcpy(&u, hd, sizeof(u));

	gnutls_free(b_holder);

	ret = gcry_mpi_set_ui(NULL, u);
	if (ret==NULL) {
		gnutls_assert();
		return NULL;
	}

	return ret;
}

/* S = (A * v^u) ^ b % N 
 * this is our shared key
 */
MPI _gnutls_calc_srp_S1(MPI A, MPI b, MPI u, MPI v, MPI n)
{
	MPI tmp1, tmp2;
	MPI S;

	S = gcry_mpi_alloc_like(n);
	if (S==NULL)
		return NULL;

	tmp1 = gcry_mpi_alloc_like(n);
	tmp2 = gcry_mpi_alloc_like(n);

	if (tmp1 == NULL || tmp2 == NULL) {
		_gnutls_mpi_release(&tmp1);
		_gnutls_mpi_release(&tmp2);
		return NULL;
	}

	gcry_mpi_powm(tmp1, v, u, n);
	gcry_mpi_mulm(tmp2, A, tmp1, n);
	_gnutls_mpi_release(&tmp1);

	gcry_mpi_powm(S, tmp2, b, n);
	_gnutls_mpi_release(&tmp2);

	return S;
}

/* A = g^a % N 
 * returns A and a (which is random)
 */
MPI _gnutls_calc_srp_A(MPI * a, MPI g, MPI n)
{
	MPI tmpa;
	MPI A;
	int bits;

	bits = gcry_mpi_get_nbits(n);
	tmpa = gcry_mpi_new(bits);	/* FIXME: allocate in secure memory */
	if (tmpa==NULL) {
		gnutls_assert();
		return NULL;
	}
	
	gcry_mpi_randomize(tmpa, bits, GCRY_STRONG_RANDOM);

	A = gcry_mpi_new(bits);	/* FIXME: allocate in secure memory */
	if (A==NULL) {
		gnutls_assert();
		_gnutls_mpi_release( &tmpa);
		return NULL;
	}
	gcry_mpi_powm(A, g, tmpa, n);

	if (a != NULL)
		*a = tmpa;
	else
		_gnutls_mpi_release(&tmpa);

	return A;
}

/* generate x = SHA(s | SHA(U | ":" | p))
 * The output is exactly 20 bytes
 */
int _gnutls_calc_srp_sha(char *username, char *password, opaque * salt,
			   int salt_size, int *size, void* digest)
{
	GNUTLS_MAC_HANDLE td;
	opaque res[MAX_HASH_SIZE];

	*size = 20;

	td = gnutls_hash_init(GNUTLS_MAC_SHA);
	if (td==NULL) {
		return GNUTLS_E_MEMORY_ERROR;
	}
	gnutls_hash(td, username, strlen(username));
	gnutls_hash(td, ":", 1);
	gnutls_hash(td, password, strlen(password));
	
	gnutls_hash_deinit(td, res);

	td = gnutls_hash_init(GNUTLS_MAC_SHA);
	if (td==NULL) {
		return GNUTLS_E_MEMORY_ERROR;
	}

	gnutls_hash(td, salt, salt_size);
	gnutls_hash(td, res, 20);	/* 20 bytes is the output of sha1 */

	gnutls_hash_deinit(td, digest);

	return 0;
}

int _gnutls_calc_srp_x(char *username, char *password, opaque * salt,
			 int salt_size, uint8 crypt_algo, int *size, void* digest)
{

	switch (crypt_algo) {
	case SRPSHA1_CRYPT:
		return _gnutls_calc_srp_sha(username, password, salt,
					    salt_size, size, digest);
	case BLOWFISH_CRYPT:
		return _gnutls_calc_srp_bcrypt(username, password, salt, salt_size,
					       size, digest);
	}
	return -1;
}


/* S = (B - g^x) ^ (a + u * x) % N
 * this is our shared key
 */
MPI _gnutls_calc_srp_S2(MPI B, MPI g, MPI x, MPI a, MPI u, MPI n)
{
	MPI S, tmp1, tmp2, tmp4;

	S = gcry_mpi_alloc_like(n);
	if (S==NULL)
		return NULL;
		
	tmp1 = gcry_mpi_alloc_like(n);
	tmp2 = gcry_mpi_alloc_like(n);
	if (tmp1 == NULL || tmp2 == NULL) {
		_gnutls_mpi_release(&tmp1);
		_gnutls_mpi_release(&tmp2);
		return NULL;
	}

	gcry_mpi_powm(tmp1, g, x, n);

	gcry_mpi_subm(tmp2, B, tmp1, n);

	tmp4 = gcry_mpi_alloc_like(n);
	if (tmp4==NULL)
		return NULL;

	gcry_mpi_mul(tmp1, u, x);
	gcry_mpi_add(tmp4, a, tmp1);
	_gnutls_mpi_release(&tmp1);

	gcry_mpi_powm(S, tmp2, tmp4, n);
	_gnutls_mpi_release(&tmp2);
	_gnutls_mpi_release(&tmp4);

	return S;
}

/**
  * gnutls_free_srp_server_sc - Used to free an allocated SRP_CLIENT_CREDENTIALS structure
  * @sc: is an &SRP_CLIENT_CREDENTIALS structure.
  *
  * This structure is complex enough to manipulate directly thus
  * this helper function is provided in order to free (deallocate)
  * the structure.
  **/
void gnutls_free_srp_client_sc( SRP_CLIENT_CREDENTIALS sc) {
	gnutls_free(sc);
}

/**
  * gnutls_allocate_srp_server_sc - Used to allocate an SRP_CLIENT_CREDENTIALS structure
  * @sc: is a pointer to an &SRP_CLIENT_CREDENTIALS structure.
  *
  * This structure is complex enough to manipulate directly thus
  * this helper function is provided in order to allocate
  * the structure.
  **/
int gnutls_allocate_srp_client_sc( SRP_CLIENT_CREDENTIALS *sc) {
	*sc = gnutls_malloc( sizeof(SRP_CLIENT_CREDENTIALS_INT));
	
	if (*sc==NULL) return GNUTLS_E_MEMORY_ERROR;

	return 0;
}

/**
  * gnutls_set_srp_client_cred - Used to set the username/password, in a SRP_CLIENT_CREDENTIALS structure
  * @res: is an &SRP_CLIENT_CREDENTIALS structure.
  * @username: is the user's userid
  * @password: is the user's password
  *
  **/
int gnutls_set_srp_client_cred( SRP_CLIENT_CREDENTIALS res, char *username, char * password) {

	res->username = gnutls_strdup( username);
	if (res->username == NULL) return GNUTLS_E_MEMORY_ERROR;

	res->password = gnutls_strdup( password);
	if (res->password==NULL) {
		gnutls_free(res->username);
		return GNUTLS_E_MEMORY_ERROR;
	}

	return 0;
}

/**
  * gnutls_free_srp_server_sc - Used to free an allocated SRP_SERVER_CREDENTIALS structure
  * @sc: is an &SRP_SERVER_CREDENTIALS structure.
  *
  * This structure is complex enough to manipulate directly thus
  * this helper function is provided in order to free (deallocate)
  * the structure.
  **/
void gnutls_free_srp_server_sc( SRP_SERVER_CREDENTIALS sc) {
	gnutls_free(sc);
}

/**
  * gnutls_allocate_srp_server_sc - Used to allocate an SRP_SERVER_CREDENTIALS structure
  * @sc: is a pointer to an &SRP_SERVER_CREDENTIALS structure.
  *
  * This structure is complex enough to manipulate directly thus
  * this helper function is provided in order to allocate
  * the structure.
  **/

int gnutls_allocate_srp_server_sc( SRP_SERVER_CREDENTIALS *sc) {
	*sc = gnutls_malloc( sizeof(SRP_SERVER_CREDENTIALS_INT));
	
	if (*sc==NULL) return GNUTLS_E_MEMORY_ERROR;
	
	return 0;
}

/**
  * gnutls_set_srp_server_cred - Used to set the password files, in a SRP_SERVER_CREDENTIALS structure
  * @res: is an &SRP_SERVER_CREDENTIALS structure.
  * @password_file: is the SRP password file (tpasswd)
  * @password_conf_file: is the SRP password conf file (tpasswd.conf)
  *
  **/
int gnutls_set_srp_server_cred( SRP_SERVER_CREDENTIALS res, char *password_file, char * password_conf_file) {

	res->password_file = gnutls_strdup( password_file);
	if (res->password_file==NULL) return GNUTLS_E_MEMORY_ERROR;
	
	res->password_conf_file = gnutls_strdup( password_conf_file);
	if (res->password_conf_file==NULL) {
		gnutls_free(res->password_file);
		return GNUTLS_E_MEMORY_ERROR;
	}

	return 0;
}


