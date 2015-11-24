// Module for coapwork

//#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "auxmods.h"
#include "lrotable.h"

#include "c_types.h"
#include "c_stdio.h"
#include "c_string.h"
#include "c_stdlib.h"
#include "os_type.h"

#include "mem.h"
#include "espconn.h"
#include "driver/uart.h"

#include "er-coap-13.h"
#include "er-coap-13-transactions.h"
#include "uri.h"
#include "pt.h"

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define COAP_PRINTF c_printf
#else
#define COAP_PRINTF
#endif

#define MAX_MESSAGE_SIZE 1152

/* Example URIs that can be queried. */
#define NUMBER_OF_URLS 4
/* leading and ending slashes only for demo purposes, get cropped automatically when setting the Uri-Path */
char* service_urls[NUMBER_OF_URLS] = {".well-known/core", "/actuators/toggle", "battery/", "error/in//path"};

struct request_state_t {
  struct pt pt;
  coap_transaction_t *transaction;
  coap_packet_t *response;
  uint32_t block_num;
};

typedef struct lcoap_userdata
{
  lua_State *L;
  struct espconn *pesp_conn;
  int self_ref;
}lcoap_userdata;

static void coap_received(void *arg, char *pdata, unsigned short len)
{
  COAP_PRINTF("coap_received is called.\n");
}

static void coap_sent(void *arg)
{
  COAP_PRINTF("coap_sent is called.\n");
}


// Lua: s = coap.create(function(conn))
static int coap_create( lua_State* L, const char* mt )
{
  struct espconn *pesp_conn = NULL;
  lcoap_userdata *cud;
  unsigned type;
  int stack = 1;

  // create a object
  cud = (lcoap_userdata *)lua_newuserdata(L, sizeof(lcoap_userdata));
  // pre-initialize it, in case of errors
  cud->self_ref = LUA_NOREF;
  cud->pesp_conn = NULL;

  // set its metatable
  luaL_getmetatable(L, mt);
  lua_setmetatable(L, -2);

  // create the espconn struct
  pesp_conn = (struct espconn *)c_zalloc(sizeof(struct espconn));
  if(!pesp_conn)
    return luaL_error(L, "not enough memory");

  cud->pesp_conn = pesp_conn;

  pesp_conn->type = ESPCONN_UDP;
  pesp_conn->proto.tcp = NULL;
  pesp_conn->proto.udp = NULL;

  pesp_conn->proto.udp = (esp_udp *)c_zalloc(sizeof(esp_udp));
  if(!pesp_conn->proto.udp){
    c_free(pesp_conn);
    cud->pesp_conn = pesp_conn = NULL;
    return luaL_error(L, "not enough memory");
  }
  pesp_conn->state = ESPCONN_NONE;
  COAP_PRINTF("UDP server/client is set.\n");

  cud->L = L;
  pesp_conn->reverse = cud;

  COAP_PRINTF("coap_create is called.\n");
  return 1;  
}

// Lua: server:delete()
static int coap_delete( lua_State* L, const char* mt )
{
  struct espconn *pesp_conn = NULL;
  lcoap_userdata *cud;

  cud = (lcoap_userdata *)luaL_checkudata(L, 1, mt);
  luaL_argcheck(L, cud, 1, "Server/Client expected");
  if(cud==NULL){
    COAP_PRINTF("userdata is nil.\n");
    return 0;
  }

  // free (unref) callback ref
  if(LUA_NOREF!=cud->self_ref){
    luaL_unref(L, LUA_REGISTRYINDEX, cud->self_ref);
    cud->self_ref = LUA_NOREF;
  }

  cud->L = NULL;
  if(cud->pesp_conn)
  {
    if(cud->pesp_conn->proto.udp->remote_port || cud->pesp_conn->proto.udp->local_port)
      espconn_delete(cud->pesp_conn);
    c_free(cud->pesp_conn->proto.udp);
    cud->pesp_conn->proto.udp = NULL;
    c_free(cud->pesp_conn);
    cud->pesp_conn = NULL;
  }

  COAP_PRINTF("coap_delete is called.\n");
  return 0;  
}

// Lua: server/client:on( "method", function(s) )
static int coap_on( lua_State* L, const char* mt )
{
  COAP_PRINTF("coap_on is called.\n");
  return 0;  
}

/*
 * ESP8266 connection response callback
 */
static void coap_response_handler(void *arg, char *pdata, unsigned short len)
{
  COAP_PRINTF("coap_response_handler is called.\n");

  struct espconn *pesp_conn = (struct espconn *)arg;
  coap_status_t rc;
  coap_packet_t response;

  uint8_t buf[MAX_MESSAGE_SIZE+1] = {0}; // +1 for string '\0'
  uint8_t token[COAP_TOKEN_LEN+1] = {0};

  c_memset(buf, 0, sizeof(buf)); // wipe prev data

  if( len > MAX_MESSAGE_SIZE )
  {
    COAP_PRINTF("Request Entity Too Large.\n"); // NOTE: should response 4.13 to client...
    return;
  }
  c_memcpy(buf, pdata, len);

  rc =  coap_parse_message(&response, buf, (uint16_t)len);

  switch (rc) {
  case BAD_REQUEST_4_00:
    COAP_PRINTF("Bad request rc=%d\n", rc);
    break;
  case NO_ERROR:
    COAP_PRINTF("Server response OK.\n");
    break;
  }

end:
  /*
   * Check token (TBD)
   */
  coap_get_header_token(&response, (const uint8_t **)&token);
}

/*
 * CoAP request transaction callbock
 */
void coap_blocking_request_callback(void *callback_data, void *response) {
  struct request_state_t *state = (struct request_state_t *) callback_data;
  state->response = (coap_packet_t*) response;
  COAP_PRINTF("coap_blocking_request_callback is called.\n");
}

coap_blocking_request(struct request_state_t *state,
                                coap_packet_t *request,
                                ip_addr_t *ipaddr,
                                coap_uri_t *uri,
                                void *context)
{
  struct espconn *pesp_conn = (struct espconn *)context;

  request->mid = coap_get_mid();
  
  if ((state->transaction = coap_new_transaction(request->mid, ipaddr, uri->port)))
  {
    state->transaction->callback = coap_blocking_request_callback;
    state->transaction->callback_data = state;
    state->transaction->context = context;

    if (state->block_num>0)
    {
      coap_set_header_block2(request, state->block_num, 0, REST_MAX_CHUNK_SIZE);
    }

    // Build CoAP header and Options
    state->transaction->packet_len = coap_serialize_message(request, state->transaction->packet);

    COAP_PRINTF("Header dump: [0x%02X %02X %02X %02X]. Size: %d\n",
        request->buffer[0],
        request->buffer[1],
        request->buffer[2],
        request->buffer[3],
        state->transaction->packet_len
      );

    coap_send_transaction(state->transaction);
    COAP_PRINTF("Requested #%lu (MID %u)\n", state->block_num, request->mid);

    coap_clear_transaction(state->transaction);
  }
  else
  {
    COAP_PRINTF("Could not allocate transaction buffer");
  }
}

// Lua: client:request( [CON], uri, [payload] )
static int coap_request( lua_State* L, coap_method_t m )
{
  struct espconn *pesp_conn = NULL;
  lcoap_userdata *cud;
  int stack = 1;
  coap_packet_t request[1]; /* This way the packet can be treated as pointer as usual. */
  struct request_state_t state[1];
  static uint8_t more;
  static uint32_t res_block;
  static uint8_t block_error;

  cud = (lcoap_userdata *)luaL_checkudata(L, stack, "coap_client");
  luaL_argcheck(L, cud, stack, "Server/Client expected");
  if(cud==NULL){
    COAP_PRINTF("userdata is nil.\n");
    return 0;
  }

  stack++;
  pesp_conn = cud->pesp_conn;
  ip_addr_t ipaddr;
  uint8_t host[32];

  unsigned t;
  if ( lua_isnumber(L, stack) )
  {
    t = lua_tointeger(L, stack);
    stack++;
    if ( t != COAP_TYPE_CON && t != COAP_TYPE_NON )
      return luaL_error( L, "wrong arg type" );
  } else {
    t = COAP_TYPE_CON; // default to CON
  }

  size_t l;
  const char *url = luaL_checklstring( L, stack, &l );
  stack++;
  if (url == NULL)
    return luaL_error( L, "wrong arg type" );

  // Get host/port from request URL
  coap_uri_t *uri = coap_new_uri(url, l);   // should call free(uri) somewhere
  if (uri == NULL)
    return luaL_error( L, "uri wrong format." );

  pesp_conn->proto.udp->remote_port = uri->port;
  COAP_PRINTF("UDP port is set: %d\n", uri->port);
  pesp_conn->proto.udp->local_port = espconn_port();

  if(uri->host.length){
    c_memcpy(host, uri->host.s, uri->host.length);
    host[uri->host.length] = '\0';

    ipaddr.addr = ipaddr_addr(host);
    COAP_PRINTF("Host len(%d):", uri->host.length);
    COAP_PRINTF(host);
    COAP_PRINTF("\n");

    c_memcpy(pesp_conn->proto.udp->remote_ip, &ipaddr.addr, 4);
    COAP_PRINTF("UDP ip is set: ");
    COAP_PRINTF(IPSTR, IP2STR(&ipaddr.addr));
    COAP_PRINTF("\n");
  }

  const char *payload = NULL;
  l = 0;
  if( lua_isstring(L, stack) ){
    payload = luaL_checklstring( L, stack, &l );
    if (payload == NULL)
      l = 0;
  }
  COAP_PRINTF("Payload: %s\n", payload);
  COAP_PRINTF("URI Path: %s\n", uri->path.s);
  COAP_PRINTF("URI Host: %s\n", host);

  coap_init_message(request, COAP_TYPE_CON, m, 0);
  coap_set_header_uri_path(request, uri->path.s);
  coap_set_header_uri_host(request, host);
  coap_set_payload(request, (uint8_t *)payload, strlen(payload));

  COAP_PRINTF("Start CoAP transaction...\n");

  /*
   * Prepare ESP8266 connections
   */
  espconn_regist_recvcb(pesp_conn, coap_response_handler);
  espconn_create(pesp_conn);

  /*
   * Invoke blocking request
   */
  coap_blocking_request(state, request, &ipaddr, uri, (void *)pesp_conn);

  if (uri)
    c_free((void *)uri);

  COAP_PRINTF("coap_request is called.\n");
  return 0;  
}

// Lua: s = coap.createClient(function(conn))
static int coap_createClient( lua_State* L )
{
  const char *mt = "coap_client";
  return coap_create(L, mt);
}

// Lua: client:gcdelete()
static int coap_client_gcdelete( lua_State* L )
{
  const char *mt = "coap_client";
  return coap_delete(L, mt);
}

// client:get( string, function(sent) )
static int coap_client_get( lua_State* L )
{
  return coap_request(L, COAP_GET);
}

// client:post( string, function(sent) )
static int coap_client_post( lua_State* L )
{
  return coap_request(L, COAP_POST);
}

// client:put( string, function(sent) )
static int coap_client_put( lua_State* L )
{
  return coap_request(L, COAP_PUT);
}

// client:delete( string, function(sent) )
static int coap_client_delete( lua_State* L )
{
  return coap_request(L, COAP_DELETE);
}

// Module function map
#define MIN_OPT_LEVEL 2
#include "lrodefs.h"

static const LUA_REG_TYPE coap_client_map[] =
{
  // -- Make a GET/POST/PUT/DELETE request
  // uri = "coap://localhost:8000/object/12345/send"
  // cc:post(uri, "{}")
  { LSTRKEY( "get" ), LFUNCVAL ( coap_client_get ) },
  { LSTRKEY( "post" ), LFUNCVAL ( coap_client_post ) },
  { LSTRKEY( "put" ), LFUNCVAL ( coap_client_put ) },
  { LSTRKEY( "delete" ), LFUNCVAL ( coap_client_delete ) },
  { LSTRKEY( "__gc" ), LFUNCVAL ( coap_client_gcdelete ) },
#if LUA_OPTIMIZE_MEMORY > 0
  { LSTRKEY( "__index" ), LROVAL ( coap_client_map ) },
#endif
  { LNILKEY, LNILVAL }
};

const LUA_REG_TYPE coap_map[] = 
{
  // -- Create a CoAP client
  // cc = coap.Client()
  { LSTRKEY( "Client" ), LFUNCVAL ( coap_createClient ) },
#if LUA_OPTIMIZE_MEMORY > 0
  { LSTRKEY( "CON" ), LNUMVAL( COAP_TYPE_CON ) },
  { LSTRKEY( "NON" ), LNUMVAL( COAP_TYPE_NON ) },

  { LSTRKEY( "__metatable" ), LROVAL( coap_map ) },
#endif
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_coap( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  luaL_rometatable(L, "coap_client", (void *)coap_client_map);  // create metatable for coap_client  
  return 0;
#else // #if LUA_OPTIMIZE_MEMORY > 0
  int n;
  luaL_register( L, AUXLIB_COAP, coap_map );

  // Set it as its own metatable
  lua_pushvalue( L, -1 );
  lua_setmetatable( L, -2 );

  // Module constants  
  MOD_REG_NUMBER( L, "CON", COAP_TYPE_CON );
  MOD_REG_NUMBER( L, "NON", COAP_TYPE_NONCON );

  n = lua_gettop(L);

  lua_settop(L, n);
  // create metatable
  luaL_newmetatable(L, "coap_client");
  // metatable.__index = metatable
  lua_pushliteral(L, "__index");
  lua_pushvalue(L,-2);
  lua_rawset(L,-3);
  // Setup the methods inside metatable
  luaL_register( L, NULL, coap_client_map );

  return 1;
#endif // #if LUA_OPTIMIZE_MEMORY > 0  
}
