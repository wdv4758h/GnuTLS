/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Nikos Mavrogiannopoulos
 *
 * This file is part of GnuTLS.
 *
 * The GnuTLS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 */

#include "gnutls_int.h"
#include "errors.h"
#include "hello_ext.h"
#include "handshake.h"
#include "mbuffers.h"
#include "tls13/encrypted_extensions.h"

int _gnutls13_recv_encrypted_extensions(gnutls_session_t session)
{
	int ret;
	gnutls_buffer_st buf;

	ret = _gnutls_recv_handshake(session, GNUTLS_HANDSHAKE_ENCRYPTED_EXTENSIONS, 0, &buf);
	if (ret < 0)
		return gnutls_assert_val(ret);

	_gnutls_handshake_log("HSK[%p]: parsing encrypted extensions\n", session);
	ret = _gnutls_parse_hello_extensions(session, GNUTLS_EXT_FLAG_EE, GNUTLS_EXT_ANY,
					     buf.data, buf.length);
	_gnutls_buffer_clear(&buf);

	if (ret < 0)
		return gnutls_assert_val(ret);

	return 0;
}

int _gnutls13_send_encrypted_extensions(gnutls_session_t session, unsigned again)
{
	int ret;
	mbuffer_st *bufel = NULL;
	gnutls_buffer_st buf;

	if (again == 0) {
		_gnutls_buffer_init(&buf);

		ret = _gnutls_gen_hello_extensions(session, &buf, GNUTLS_EXT_FLAG_EE, GNUTLS_EXT_ANY);
		if (ret < 0)
			return gnutls_assert_val(ret);

		bufel = _gnutls_handshake_alloc(session, buf.length);
		if (bufel == NULL)
			return gnutls_assert_val(GNUTLS_E_MEMORY_ERROR);

		_mbuffer_set_udata_size(bufel, 0);
		ret = _mbuffer_append_data(bufel, buf.data, buf.length);
		if (ret < 0) {
			gnutls_assert();
			goto cleanup;
		}

		_gnutls_buffer_clear(&buf);
	}

	return _gnutls_send_handshake(session, bufel, GNUTLS_HANDSHAKE_ENCRYPTED_EXTENSIONS);

 cleanup:
	_gnutls_buffer_clear(&buf);
	_mbuffer_xfree(&bufel);
	return ret;
}