#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <utils.h>
#include <stdlib.h>
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#include <gnutls/abstract.h>
#include <gnutls/x509.h>

GNUTLS_NO_FIPS140_MODE

/* This does check operation when the FIPS140 support
 * is explicitly disabled.
 */

static void tls_log_func(int level, const char *str)
{
	fprintf(stderr, "<%d>| %s", level, str);
}

static uint8_t key16[16];
static uint8_t iv16[16];

void doit(void)
{
	int ret;
	unsigned int mode;
	gnutls_cipher_hd_t ch;
	gnutls_hmac_hd_t mh;
	gnutls_session_t session;
	gnutls_pubkey_t pubkey;
	gnutls_x509_privkey_t xprivkey;
	gnutls_privkey_t privkey;
	gnutls_datum_t key = { key16, sizeof(key16) };
	gnutls_datum_t iv = { iv16, sizeof(iv16) };

	gnutls_global_set_log_function(tls_log_func);
	if (debug)
		gnutls_global_set_log_level(4711);

	mode = gnutls_fips140_mode_enabled();
	if (mode != 0) {
		success("We are in FIPS140 mode, although we shouldn't\n");
		exit(1);
	}

	ret = global_init();
	if (ret < 0) {
		fail("Cannot initialize library\n");
	}

	/* Try crypto.h functionality */
	ret =
	    gnutls_cipher_init(&ch, GNUTLS_CIPHER_AES_128_CBC, &key, &iv);
	if (ret < 0) {
		fail("gnutls_cipher_init failed for aes\n");
	}
	gnutls_cipher_deinit(ch);

	ret =
	    gnutls_cipher_init(&ch, GNUTLS_CIPHER_CAMELLIA_128_CBC, &key, &iv);
	if (ret < 0) {
		fail("gnutls_cipher_init failed for camellia\n");
	}
	gnutls_cipher_deinit(ch);

	ret = gnutls_hmac_init(&mh, GNUTLS_MAC_SHA1, key.data, key.size);
	if (ret < 0) {
		fail("gnutls_hmac_init failed for SHA1\n");
	}
	gnutls_hmac_deinit(mh, NULL);

	ret = gnutls_hmac_init(&mh, GNUTLS_MAC_MD5, key.data, key.size);
	if (ret < 0) {
		fail("gnutls_hmac_init failed for MD5\n");
	}
	gnutls_hmac_deinit(mh, NULL);

	ret = gnutls_rnd(GNUTLS_RND_NONCE, key16, sizeof(key16));
	if (ret < 0) {
		fail("gnutls_rnd failed\n");
	}

	ret = gnutls_pubkey_init(&pubkey);
	if (ret < 0) {
		fail("gnutls_pubkey_init failed\n");
	}
	gnutls_pubkey_deinit(pubkey);

	ret = gnutls_privkey_init(&privkey);
	if (ret < 0) {
		fail("gnutls_privkey_init failed\n");
	}
	gnutls_privkey_deinit(privkey);

	ret = gnutls_x509_privkey_init(&xprivkey);
	if (ret < 0) {
		fail("gnutls_privkey_init failed\n");
	}
	gnutls_x509_privkey_deinit(xprivkey);

	ret = gnutls_init(&session, 0);
	if (ret < 0) {
		fail("gnutls_init failed\n");
	}
	gnutls_deinit(session);

	gnutls_global_deinit();
	return;
}
