/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      An implementation of the Constrained Application Protocol (draft 12)
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#ifndef COAP_13_H_
#define COAP_13_H_

#include "c_stdio.h"
#include "c_string.h"
#include "c_stdlib.h"
#include "os_type.h"

#include "espconn.h"

#define COAP_LINK_FORMAT_FILTERING           1

#define COAP_DEFAULT_PORT                    5683
#define COAP_DEFAULT_SCHEME        "coap" /* the default scheme for CoAP URIs */

#ifndef COAP_SERVER_PORT
#define COAP_SERVER_PORT                     COAP_DEFAULT_PORT
#endif

/*
 * The maximum buffer size that is provided for resource responses and must be respected due to the limited IP buffer.
 * Larger data must be handled by the resource and will be sent chunk-wise through a TCP stream or CoAP blocks.
 */
#ifndef REST_MAX_CHUNK_SIZE
#define REST_MAX_CHUNK_SIZE     128
#endif

#define COAP_DEFAULT_MAX_AGE                 60
#define COAP_RESPONSE_TIMEOUT                2
#define COAP_RESPONSE_RANDOM_FACTOR          1.5
#define COAP_MAX_RETRANSMIT                  4

#define COAP_HEADER_LEN                      4 /* | version:0x03 type:0x0C tkl:0xF0 | code | mid:0x00FF | mid:0xFF00 | */
#define COAP_ETAG_LEN                        8 /* The maximum number of bytes for the ETag */
#define COAP_TOKEN_LEN                       8 /* The maximum number of bytes for the Token */
#define COAP_MAX_ACCEPT_NUM                  2 /* The maximum number of accept preferences to parse/store */

#define COAP_HEADER_VERSION_MASK             0xC0
#define COAP_HEADER_VERSION_POSITION         6
#define COAP_HEADER_TYPE_MASK                0x30
#define COAP_HEADER_TYPE_POSITION            4
#define COAP_HEADER_TOKEN_LEN_MASK           0x0F
#define COAP_HEADER_TOKEN_LEN_POSITION       0

#define COAP_HEADER_OPTION_DELTA_MASK        0xF0
#define COAP_HEADER_OPTION_SHORT_LENGTH_MASK 0x0F

/** The maximum transmission unit at the IP Layer*/
#define UIP_LINK_MTU 1280

/**
 * The link level header length.
 *
 * This is the offset into the uip_buf where the IP header can be
 * found. For Ethernet, this should be set to 14. For SLIP, this
 * should be set to 0.
 *
 * \note we probably won't use this constant for other link layers than
 * ethernet as they have variable header length (this is due to variable
 * number and type of address fields and to optional security features)
 * E.g.: 802.15.4 -> 2 + (1/2*4/8) + 0/5/6/10/14
 *       802.11 -> 4 + (6*3/4) + 2
 * \hideinitializer
 */

#ifdef UIP_CONF_LLH_LEN
#define UIP_LLH_LEN (UIP_CONF_LLH_LEN)
#else /* UIP_LLH_LEN */
#define UIP_LLH_LEN     0
#endif /* UIP_CONF_LLH_LEN */
 
/**
 * The size of the uIP packet buffer.
 *
 * The uIP packet buffer should not be smaller than 60 bytes, and does
 * not need to be larger than 1514 bytes. Lower size results in lower
 * TCP throughput, larger size results in higher TCP throughput.
 *
 * \hideinitializer
 */
#ifndef UIP_CONF_BUFFER_SIZE
#define UIP_BUFSIZE (UIP_LINK_MTU + UIP_LLH_LEN)
#else /* UIP_CONF_BUFFER_SIZE */
#define UIP_BUFSIZE (UIP_CONF_BUFFER_SIZE)
#endif /* UIP_CONF_BUFFER_SIZE */

#define UIP_IPH_LEN    20    /* Size of IP header */
#define UIP_UDPH_LEN    8    /* Size of UDP header */
#define UIP_IPUDPH_LEN (UIP_UDPH_LEN + UIP_IPH_LEN)    /* Size of IP +
                                                          * UDP
                                                          * header */
/*
 * Conservative size limit, as not all options have to be set at the same time.
 */
#ifndef COAP_MAX_HEADER_SIZE
/*                            Hdr CoT Age  Tag              Obs  Tok               Blo strings */
#define COAP_MAX_HEADER_SIZE  (4 + 3 + 5 + 1+COAP_ETAG_LEN + 3 + 1+COAP_TOKEN_LEN + 4 + 30) /* 70 */
#endif /* COAP_MAX_HEADER_SIZE */

#define COAP_MAX_PACKET_SIZE  (COAP_MAX_HEADER_SIZE + REST_MAX_CHUNK_SIZE)
/*                                        0/14          48 for IPv6 (28 for IPv4) */
#if COAP_MAX_PACKET_SIZE > (UIP_BUFSIZE - UIP_LLH_LEN - UIP_IPUDPH_LEN)
#error "UIP_CONF_BUFFER_SIZE too small for REST_MAX_CHUNK_SIZE"
#endif

/*
 * Maximum number of failed request attempts before action
 */
#ifndef COAP_MAX_ATTEMPTS
#define COAP_MAX_ATTEMPTS             4
#endif /* COAP_MAX_ATTEMPTS */

#define UIP_IP_BUF    ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF   ((struct uip_udp_hdr *)&uip_buf[UIP_LLH_LEN + UIP_IPH_LEN])

/* Bitmap for set options */
enum { OPTION_MAP_SIZE = sizeof(uint8_t) * 8 };
#define SET_OPTION(packet, opt) ((packet)->options[opt / OPTION_MAP_SIZE] |= 1 << (opt % OPTION_MAP_SIZE))
#define IS_OPTION(packet, opt) ((packet)->options[opt / OPTION_MAP_SIZE] & (1 << (opt % OPTION_MAP_SIZE)))

#ifndef MIN
#define MIN(a, b) ((a) < (b)? (a) : (b))
#endif /* MIN */

/* CoAP message types */
typedef enum {
  COAP_TYPE_CON, /* confirmables */
  COAP_TYPE_NON, /* non-confirmables */
  COAP_TYPE_ACK, /* acknowledgements */
  COAP_TYPE_RST  /* reset */
} coap_message_type_t;

/* CoAP request method codes */
typedef enum {
  COAP_GET = 1,
  COAP_POST,
  COAP_PUT,
  COAP_DELETE
} coap_method_t;

/* CoAP response codes */
typedef enum {
  NO_ERROR = 0,

  CREATED_2_01 = 65,                    /* CREATED */
  DELETED_2_02 = 66,                    /* DELETED */
  VALID_2_03 = 67,                      /* NOT_MODIFIED */
  CHANGED_2_04 = 68,                    /* CHANGED */
  CONTENT_2_05 = 69,                    /* OK */

  BAD_REQUEST_4_00 = 128,               /* BAD_REQUEST */
  UNAUTHORIZED_4_01 = 129,              /* UNAUTHORIZED */
  BAD_OPTION_4_02 = 130,                /* BAD_OPTION */
  FORBIDDEN_4_03 = 131,                 /* FORBIDDEN */
  NOT_FOUND_4_04 = 132,                 /* NOT_FOUND */
  METHOD_NOT_ALLOWED_4_05 = 133,        /* METHOD_NOT_ALLOWED */
  NOT_ACCEPTABLE_4_06 = 134,            /* NOT_ACCEPTABLE */
  PRECONDITION_FAILED_4_12 = 140,       /* BAD_REQUEST */
  REQUEST_ENTITY_TOO_LARGE_4_13 = 141,  /* REQUEST_ENTITY_TOO_LARGE */
  UNSUPPORTED_MEDIA_TYPE_4_15 = 143,    /* UNSUPPORTED_MEDIA_TYPE */

  INTERNAL_SERVER_ERROR_5_00 = 160,     /* INTERNAL_SERVER_ERROR */
  NOT_IMPLEMENTED_5_01 = 161,           /* NOT_IMPLEMENTED */
  BAD_GATEWAY_5_02 = 162,               /* BAD_GATEWAY */
  SERVICE_UNAVAILABLE_5_03 = 163,       /* SERVICE_UNAVAILABLE */
  GATEWAY_TIMEOUT_5_04 = 164,           /* GATEWAY_TIMEOUT */
  PROXYING_NOT_SUPPORTED_5_05 = 165,    /* PROXYING_NOT_SUPPORTED */

  /* Erbium errors */
  MEMORY_ALLOCATION_ERROR = 192,
  PACKET_SERIALIZATION_ERROR,

  /* Erbium hooks */
  MANUAL_RESPONSE

} coap_status_t;

/* CoAP header options */
typedef enum {
  COAP_OPTION_IF_MATCH = 1,       /* 0-8 B */
  COAP_OPTION_URI_HOST = 3,       /* 1-255 B */
  COAP_OPTION_ETAG = 4,           /* 1-8 B */
  COAP_OPTION_IF_NONE_MATCH = 5,  /* 0 B */
  COAP_OPTION_OBSERVE = 6,        /* 0-3 B */
  COAP_OPTION_URI_PORT = 7,       /* 0-2 B */
  COAP_OPTION_LOCATION_PATH = 8,  /* 0-255 B */
  COAP_OPTION_URI_PATH = 11,      /* 0-255 B */
  COAP_OPTION_CONTENT_TYPE = 12,  /* 0-2 B */
  COAP_OPTION_MAX_AGE = 14,       /* 0-4 B */
  COAP_OPTION_URI_QUERY = 15,     /* 0-270 B */
  COAP_OPTION_ACCEPT = 16,        /* 0-2 B */
  COAP_OPTION_TOKEN = 19,         /* 1-8 B */
  COAP_OPTION_LOCATION_QUERY = 20, /* 1-270 B */
  COAP_OPTION_BLOCK2 = 23,        /* 1-3 B */
  COAP_OPTION_BLOCK1 = 27,        /* 1-3 B */
  COAP_OPTION_SIZE = 28,          /* 0-4 B */
  COAP_OPTION_PROXY_URI = 35,     /* 1-270 B */
} coap_option_t;

/* CoAP Content-Types */
typedef enum {
  TEXT_PLAIN = 0,
    TEXT_XML = 1, /* Indented types are not in the initial registry. */
    TEXT_CSV = 2,
    TEXT_HTML = 3,
    IMAGE_GIF = 21,
    IMAGE_JPEG = 22,
    IMAGE_PNG = 23,
    IMAGE_TIFF = 24,
    AUDIO_RAW = 25,
    VIDEO_RAW = 26,
  APPLICATION_LINK_FORMAT = 40,
  APPLICATION_XML = 41,
  APPLICATION_OCTET_STREAM = 42,
    APPLICATION_RDF_XML = 43,
    APPLICATION_SOAP_XML = 44,
    APPLICATION_ATOM_XML = 45,
    APPLICATION_XMPP_XML = 46,
  APPLICATION_EXI = 47,
    APPLICATION_FASTINFOSET = 48,
    APPLICATION_SOAP_FASTINFOSET = 49,
  APPLICATION_JSON = 50,
    APPLICATION_X_OBIX_BINARY = 51
} coap_content_type_t;

typedef struct _multi_option_t {
  struct _multi_option_t *next;
  uint8_t is_static;
  uint8_t len;
  uint8_t *data;
} multi_option_t;

/* Parsed message struct */
typedef struct {
  uint8_t *buffer; /* pointer to CoAP header / incoming packet buffer / memory to serialize packet */

  uint8_t version;
  coap_message_type_t type;
  uint8_t code;
  uint16_t mid;

  uint8_t options[COAP_OPTION_PROXY_URI / OPTION_MAP_SIZE + 1]; /* Bitmap to check if option is set */
  const char *url;
  size_t url_len;

  coap_content_type_t content_type; /* Parse options once and store; allows setting options in random order  */
  uint32_t max_age;
  size_t proxy_uri_len;
  const char *proxy_uri;
  uint8_t etag_len;
  uint8_t etag[COAP_ETAG_LEN];
  size_t uri_host_len;
  const char *uri_host;
  multi_option_t *location_path;
  uint16_t uri_port;
  size_t location_query_len;
  uint8_t *location_query;
  multi_option_t *uri_path;
  uint16_t observe;
  uint8_t token_len;
  uint8_t token[COAP_TOKEN_LEN];
  uint8_t accept_num;
  uint16_t accept[COAP_MAX_ACCEPT_NUM];
  uint8_t if_match_len;
  uint8_t if_match[COAP_ETAG_LEN];
  uint32_t block2_num;
  uint8_t block2_more;
  uint16_t block2_size;
  uint32_t block2_offset;
  uint32_t block1_num;
  uint8_t block1_more;
  uint16_t block1_size;
  uint32_t block1_offset;
  uint32_t size;
  multi_option_t *uri_query;
  uint8_t if_none_match;

  uint16_t payload_len;
  uint8_t *payload;

} coap_packet_t;

/* Option format serialization*/
#define COAP_SERIALIZE_INT_OPTION(number, field, text)  \
    if (IS_OPTION(coap_pkt, number)) { \
      PRINTF(text" [%u]\n", coap_pkt->field); \
      option += coap_serialize_int_option(number, current_number, option, coap_pkt->field); \
      current_number = number; \
    }
#define COAP_SERIALIZE_BYTE_OPTION(number, field, text)      \
    if (IS_OPTION(coap_pkt, number)) { \
      PRINTF(text" %u [0x%02X%02X%02X%02X%02X%02X%02X%02X]\n", coap_pkt->field##_len, \
        coap_pkt->field[0], \
        coap_pkt->field[1], \
        coap_pkt->field[2], \
        coap_pkt->field[3], \
        coap_pkt->field[4], \
        coap_pkt->field[5], \
        coap_pkt->field[6], \
        coap_pkt->field[7] \
      ); /*FIXME always prints 8 bytes */ \
      option += coap_serialize_array_option(number, current_number, option, coap_pkt->field, coap_pkt->field##_len, '\0'); \
      current_number = number; \
    }
#define COAP_SERIALIZE_STRING_OPTION(number, field, splitter, text)      \
    if (IS_OPTION(coap_pkt, number)) { \
      PRINTF(text" [%.*s]\n", coap_pkt->field##_len, coap_pkt->field); \
      option += coap_serialize_array_option(number, current_number, option, (uint8_t *) coap_pkt->field, coap_pkt->field##_len, splitter); \
      current_number = number; \
    }
#define COAP_SERIALIZE_MULTI_OPTION(number, field, text)      \
        if (IS_OPTION(coap_pkt, number)) { \
          PRINTF(text); \
          option += coap_serialize_multi_option(number, current_number, option, coap_pkt->field); \
          current_number = number; \
        }
#define COAP_SERIALIZE_ACCEPT_OPTION(number, field, text)  \
    if (IS_OPTION(coap_pkt, number)) { \
      int i; \
      for (i=0; i<coap_pkt->field##_num; ++i) \
      { \
        PRINTF(text" [%u]\n", coap_pkt->field[i]); \
        option += coap_serialize_int_option(number, current_number, option, coap_pkt->field[i]); \
        current_number = number; \
      } \
    }
#define COAP_SERIALIZE_BLOCK_OPTION(number, field, text)      \
    if (IS_OPTION(coap_pkt, number)) \
    { \
      PRINTF(text" [%lu%s (%u B/blk)]\n", coap_pkt->field##_num, coap_pkt->field##_more ? "+" : "", coap_pkt->field##_size); \
      uint32_t block = coap_pkt->field##_num << 4; \
      if (coap_pkt->field##_more) block |= 0x8; \
      block |= 0xF & coap_log_2(coap_pkt->field##_size/16); \
      PRINTF(text" encoded: 0x%lX\n", block); \
      option += coap_serialize_int_option(number, current_number, option, block); \
      current_number = number; \
    }

/* To store error code and human-readable payload */
extern coap_status_t coap_error_code;
extern char *coap_error_message;

void coap_init_connection(uint16_t port);
uint16_t coap_get_mid(void);

void coap_init_message(void *packet, coap_message_type_t type, uint8_t code, uint16_t mid);
size_t coap_serialize_message(void *packet, uint8_t *buffer);
coap_status_t coap_parse_message(void *request, uint8_t *data, uint16_t data_len);
void coap_free_header(void *packet);

char * coap_get_multi_option_as_string(multi_option_t * option);

int coap_get_query_variable(void *packet, const char *name, const char **output);
int coap_get_post_variable(void *packet, const char *name, const char **output);

/*-----------------------------------------------------------------------------------*/

int coap_set_status_code(void *packet, unsigned int code);

unsigned int coap_get_header_content_type(void *packet);
int coap_set_header_content_type(void *packet, unsigned int content_type);

int coap_get_header_accept(void *packet, const uint16_t **accept);
int coap_set_header_accept(void *packet, uint16_t accept);

int coap_get_header_max_age(void *packet, uint32_t *age);
int coap_set_header_max_age(void *packet, uint32_t age);

int coap_get_header_etag(void *packet, const uint8_t **etag);
int coap_set_header_etag(void *packet, const uint8_t *etag, size_t etag_len);

int coap_get_header_if_match(void *packet, const uint8_t **etag);
int coap_set_header_if_match(void *packet, const uint8_t *etag, size_t etag_len);

int coap_get_header_if_none_match(void *packet);
int coap_set_header_if_none_match(void *packet);

int coap_get_header_token(void *packet, const uint8_t **token);
int coap_set_header_token(void *packet, const uint8_t *token, size_t token_len);

int coap_get_header_proxy_uri(void *packet, const char **uri); /* In-place string might not be 0-terminated. */
int coap_set_header_proxy_uri(void *packet, const char *uri);

int coap_get_header_uri_host(void *packet, const char **host); /* In-place string might not be 0-terminated. */
int coap_set_header_uri_host(void *packet, const char *host);

int coap_get_header_uri_path(void *packet, const char **path); /* In-place string might not be 0-terminated. */
int coap_set_header_uri_path(void *packet, const char *path);

int coap_get_header_uri_query(void *packet, const char **query); /* In-place string might not be 0-terminated. */
int coap_set_header_uri_query(void *packet, const char *query);

int coap_get_header_location_path(void *packet, const char **path); /* In-place string might not be 0-terminated. */
int coap_set_header_location_path(void *packet, const char *path); /* Also splits optional query into Location-Query option. */

int coap_get_header_location_query(void *packet, const char **query); /* In-place string might not be 0-terminated. */
int coap_set_header_location_query(void *packet, char *query);

int coap_get_header_observe(void *packet, uint32_t *observe);
int coap_set_header_observe(void *packet, uint32_t observe);

int coap_get_header_block2(void *packet, uint32_t *num, uint8_t *more, uint16_t *size, uint32_t *offset);
int coap_set_header_block2(void *packet, uint32_t num, uint8_t more, uint16_t size);

int coap_get_header_block1(void *packet, uint32_t *num, uint8_t *more, uint16_t *size, uint32_t *offset);
int coap_set_header_block1(void *packet, uint32_t num, uint8_t more, uint16_t size);

int coap_get_header_size(void *packet, uint32_t *size);
int coap_set_header_size(void *packet, uint32_t size);

int coap_get_payload(void *packet, const uint8_t **payload);
int coap_set_payload(void *packet, const void *payload, size_t length);

#endif /* COAP_13_H_ */