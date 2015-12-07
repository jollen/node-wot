/*
 MIT License (MIT)

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include <string.h>

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

#include "liblwm2m.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define LWM2M_LWM2M_PRINTF(...) printf(__VA_ARGS__)
#else
#define LWM2M_LWM2M_PRINTF(...)
#endif

/**
 * lwm2m lua object
 */
#define LWM2M_STRING  0x01
#define LWM2M_NUMBER  0x02
#define LWM2M_BOOLEAN 0x03

typedef struct luaobject_userdata {
  lua_State * L;
  int tableref;
} luaobject_userdata;

// Push the instance with the given instanceId on the lua stack
static int prv_get_instance(lua_State * L, luaobject_userdata * userdata,
    uint16_t instanceId) {
  // Get table of this object on the stack.
  lua_rawgeti(L, LUA_REGISTRYINDEX, userdata->tableref); // stack: ..., object
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }

  // Get instance
  lua_pushinteger(L, instanceId); // stack: ..., object, instanceId
  lua_gettable(L, -2); // stack: ..., object, instance
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return 0;
  }

  // Remove object of the stack
  lua_remove(L, -2);  // stack: ..., instance

  return 1;
}

// get the type of the resource of with the given resourceid of
// the instance on top of the stack
static int prv_get_type(lua_State * L, uint16_t resourceid) {
  // Call the list function
  lua_getfield(L, -1, "type"); // stack: ..., instance, typeFunc

  // type field should be a function
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return 0;
  }

  // Push instance and resource id on the stack and call the typeFunc
  lua_pushvalue(L, -2);  // stack: ..., instance, typeFunc, instance
  lua_pushinteger(L, resourceid);  // stack: ..., instance, typeFunc, resourceid
  lua_call(L, 2, 1); // stack: ..., instance, type

  if (!lua_isnumber(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return -1;
  }

  int type = lua_tointeger(L,-1);
  lua_pop(L, 1); // stack: ..., instance

  return type;
}

// Push a lua list on the stack of all resourceId available for the instance on the stack
static int prv_get_resourceId_list(lua_State * L) {
  // Call the list function
  lua_getfield(L, -1, "list"); // stack: ..., instance, listFunc

  // list field should be a function
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return 0;
  }

  // Push instance on the stack and call the listFunc
  lua_pushvalue(L, -2);  // stack: ..., instance, listFunc, instance
  lua_call(L, 1, 1); // stack: ..., instance, list

  if (!lua_istable(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return 0;
  }

  return 1;
}

// Convert the resource from the top of the stack in dataP
// return 0 (COAP_NO_ERROR) if ok or COAP error if an error occurred (see liblwm2m.h)
static int prv_luaToResourceData(lua_State * L, uint16_t resourceid,
    lwm2m_data_t * dataP, lwm2m_data_type_t type) {
  int value_type = lua_type(L, -1);
  switch (value_type) {
  case LUA_TNIL:
    dataP->id = resourceid;
    dataP->value = NULL;
    dataP->length = 0;
    dataP->type = type;
    break;
  case LUA_TBOOLEAN:
    dataP->id = resourceid;
    dataP->type = type;
    int64_t boolean = lua_toboolean(L, -1);
    if (boolean)
      lwm2m_data_encode_int(1,dataP);
    else
      lwm2m_data_encode_int(0,dataP);
    break;
  case LUA_TNUMBER:
    dataP->id = resourceid;
    dataP->type = type;
    int64_t number = lua_tointeger(L,-1);
    lwm2m_data_encode_int(number,dataP);
    break;
  case LUA_TSTRING:
    dataP->id = resourceid;
    dataP->value = strdup(lua_tolstring(L, -1, &dataP->length));
    dataP->type = type;
    if (dataP->value == NULL) {
      // Manage memory allocation error
      return COAP_500_INTERNAL_SERVER_ERROR ;
    }
    break;
  case LUA_TTABLE:
    if (type == LWM2M_TYPE_RESOURCE_INSTANCE)
      return COAP_500_INTERNAL_SERVER_ERROR ;

    // First iteration to get the number of resource instance
    int size = 0;
    lua_pushnil(L); // stack: ..., resourceValue, nil
    while (lua_next(L, -2) != 0) { // stack: ...,resourceValue , key, value
      if (lua_isnumber(L, -2)) {
        size++;
      }
      // Removes 'value'; keeps 'key' for next iteration
      lua_pop(L, 1); // stack: ...,resourceValue , key
    }

    // Second iteration to convert value to data
    lwm2m_data_t * subdataP = lwm2m_data_new(size);
    if (size > 0) {
      lua_pushnil(L); // stack: ..., resourceValue, nil
      int i = 0;
      while (lua_next(L, -2) != 0) { // stack: ...,resourceValue , key, value
        if (lua_isnumber(L, -2)) {
          int err = prv_luaToResourceData(L, lua_tointeger(L, -2),
              &subdataP[i], LWM2M_TYPE_RESOURCE_INSTANCE);
          i++;
          if (err) {
            lua_pop(L, 2);
            return err;
          }
        }
        // Removes 'value'; keeps 'key' for next iteration
        lua_pop(L, 1); // stack: ...,resourceValue , key
      }
    }

    // Update Data struct
    dataP->id = resourceid;
    dataP->type = LWM2M_TYPE_MULTIPLE_RESOURCE;
    dataP->value = (uint8_t *) subdataP;
    dataP->length = size;
    break;
  default:
    // Other type is not managed for now.
    return COAP_501_NOT_IMPLEMENTED ;
    break;
  }
  return COAP_NO_ERROR ;
}

// Read the resource of the instance on the top of the stack.
static uint8_t prv_read_resource(lua_State * L, uint16_t resourceid,
    lwm2m_data_t * dataP) {

  // Get the read function
  lua_getfield(L, -1, "read"); // stack: ..., instance, readFunc
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return COAP_500_INTERNAL_SERVER_ERROR ;
  }

  // Push instance and resource id on the stack and call the readFunc
  lua_pushvalue(L, -2);  // stack: ..., instance, readFunc, instance
  lua_pushinteger(L, resourceid); // stack: ..., instance, readFunc, instance, resourceId
  lua_call(L, 2, 2); // stack: ..., instance, return_code, value

  // Get return code
  int ret = lua_tointeger(L, -2);
  if (ret == COAP_205_CONTENT) {
    int err = prv_luaToResourceData(L, resourceid, dataP,
    LWM2M_TYPE_RESOURCE);
    if (err)
      ret = err;
  }

  // clean the stack
  lua_pop(L, 2);
  return ret;
}

static uint8_t prv_read(uint16_t instanceId, int * numDataP,
    lwm2m_data_t ** dataArrayP, lwm2m_object_t * objectP) {

  // Get user data.
  luaobject_userdata * userdata = (luaobject_userdata*) objectP->userData;
  lua_State * L = userdata->L;

  // Push instance on the stack
  int res = prv_get_instance(L, userdata, instanceId); // stack: ..., instance
  if (!res)
    return COAP_404_NOT_FOUND ;

  if ((*numDataP) == 0) {
    // Push resourceId list on the stack
    int res = prv_get_resourceId_list(L); // stack : ..., instance, resourceList
    if (!res) {
      lua_pop(L, 1);
      return COAP_500_INTERNAL_SERVER_ERROR ;
    }

    // Get number of resource
    size_t nbRes = lua_objlen(L, -1);

    // Create temporary structure
    lwm2m_data_t tmpDataArray[nbRes];
    c_memset(tmpDataArray, 0, nbRes * sizeof(lwm2m_data_t));

    // Iterate through all items of the resourceId list
    int i = 0;
    lua_pushnil(L); // stack: ..., instance, resourceList, key(nil)
    while (lua_next(L, -2) != 0) { // stack: ...,instance , resourceList, key, value
      if (lua_isnumber(L, -1)) {
        int resourceid = lua_tointeger(L, -1);
        lua_pushvalue(L, -4); // stack: ...,instance , resourceList, key, value, instance
        int res = prv_read_resource(L, resourceid, &tmpDataArray[i]);
        if (res <= COAP_205_CONTENT)
          i++;
        lua_pop(L, 1); // stack: ...,instance , resourceList, key, value
      }
      // Removes 'value'; keeps 'key' for next iteration
      lua_pop(L, 1); // stack: ...,instance , resourceList, key
    }
    // Clean the stack
    lua_pop(L, 2);

    // Allocate memory for this resource
    *dataArrayP = lwm2m_data_new(i);
    if (*dataArrayP == NULL)
      return COAP_500_INTERNAL_SERVER_ERROR ;

    // Copy data in output parameter
    (*numDataP) = i;
    c_memcpy(*dataArrayP, tmpDataArray, sizeof(lwm2m_data_t) * i);

    return COAP_205_CONTENT ;
  } else {
    // Get resource.
    int ret;
    int i = 0;
    do{
      ret = prv_read_resource(L, ((*dataArrayP)+i)->id, (*dataArrayP)+i);
      i++;
    }while (i < *numDataP && ret == COAP_205_CONTENT);
    lua_pop(L, 1);
    return ret;
  }

  lua_pop(L, 1);
  return COAP_501_NOT_IMPLEMENTED ;
}

// Read the resource of the instance on the top of the stack.
static uint8_t prv_write_resource(lua_State * L, uint16_t resourceid,
    lwm2m_data_t data) {
  // get resource type
  int type = prv_get_type(L,resourceid);
  // Get the write function
  lua_getfield(L, -1, "write"); // stack: ..., instance, writeFunc
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return COAP_500_INTERNAL_SERVER_ERROR ;
  }

  // Push instance and resource id on the stack and call the writeFunc
  lua_pushvalue(L, -2);  // stack: ..., instance, writeFunc, instance
  lua_pushinteger(L, resourceid); // stack: ..., instance, writeFunc, instance, resourceId

  // decode and push value
  if (type == LWM2M_STRING)
    lua_pushlstring(L, data.value, data.length);
  else{
    int64_t val = 0;
    int res = lwm2m_data_decode_int(&data, &val);
    if (res != 1){
      // unable to decode int
      lua_pop(L,3);
      return COAP_400_BAD_REQUEST;
    }
    if (type == LWM2M_BOOLEAN){
      lua_pushboolean(L,val);
    }else if (type == LWM2M_NUMBER){
      lua_pushinteger(L,val);
    }else{
      lua_pop(L,3);
      return COAP_500_INTERNAL_SERVER_ERROR;
    }
  }// stack: ..., instance, writeFunc, instance, resourceId, value

  lua_call(L, 3, 1); // stack: ..., instance, return_code

  // Get return code
  int ret = lua_tointeger(L, -1);

  // Clean the stack
  lua_pop(L, 1);
  return ret;
}

static uint8_t prv_write(uint16_t instanceId, int numData,
    lwm2m_data_t * dataArray, lwm2m_object_t * objectP) {
  // Get user data.
  luaobject_userdata * userdata = (luaobject_userdata*) objectP->userData;
  lua_State * L = userdata->L;

  // Push instance on the stack
  int res = prv_get_instance(L, userdata, instanceId);
  if (!res)
    return COAP_500_INTERNAL_SERVER_ERROR ;

  // write resource
  int i = 0;
  int result;
  do {
    result = prv_write_resource(userdata->L, dataArray[i].id, dataArray[i]);
    i++;
  } while (i < numData && result == COAP_204_CHANGED );
  lua_pop(L, 1);
  return result;
}

static uint8_t prv_execute_resource(lua_State * L, uint16_t resourceid) {
  // Get the execute_function
  lua_getfield(L, -1, "execute"); // stack: ..., instance, executeFunc
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return COAP_500_INTERNAL_SERVER_ERROR ;
  }

  // Push instance and resource id on the stack and call the executeFunc
  lua_pushvalue(L, -2);  // stack: ..., instance, executeFunc, instance
  lua_pushinteger(L, resourceid); // stack: ..., instance, executeFunc, instance, resourceId
  lua_call(L, 2, 1); // stack: ..., instance, return_code

  // get return code
  int ret = lua_tointeger(L, -1);

  // clean the stack
  lua_pop(L, 1);
  return ret;
}

static uint8_t prv_execute(uint16_t instanceId, uint16_t resourceId,
        uint8_t * buffer, int length, lwm2m_object_t * objectP) {
  // Get user data.
  luaobject_userdata * userdata = (luaobject_userdata*) objectP->userData;
  lua_State * L = userdata->L;

  // Push instance on the stack
  int res = prv_get_instance(L, userdata, instanceId);
  if (!res)
    return COAP_500_INTERNAL_SERVER_ERROR ;

  // execute the given resource for the given id
  if (instanceId == 0) {
    int ret = prv_execute_resource(userdata->L, resourceId);
    lua_pop(L, 1);
    return ret;
  } else {
    // TODO : manage multi-instance.
    lua_pop(L, 1);
    return COAP_501_NOT_IMPLEMENTED ;
  }
  lua_pop(L, 1);
  return COAP_501_NOT_IMPLEMENTED ;
}

static uint8_t prv_delete(uint16_t id, lwm2m_object_t * objectP) {
  // Remove instance in C list
  lwm2m_list_t * deletedInstance;
  objectP->instanceList = lwm2m_list_remove(objectP->instanceList, id,
      (lwm2m_list_t **) &deletedInstance);
  if (NULL == deletedInstance)
    return COAP_404_NOT_FOUND ;

  c_free(deletedInstance);

  // Get user data.
  luaobject_userdata * userdata = (luaobject_userdata*) objectP->userData;
  lua_State * L = userdata->L;

  // Push instance on the stack
  int res = prv_get_instance(L, userdata, id); // stack: ..., instance
  if (!res)
    return COAP_500_INTERNAL_SERVER_ERROR ;

  // Get the delete function
  lua_getfield(L, -1, "delete"); // stack: ..., instance, deleteFunc
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return COAP_500_INTERNAL_SERVER_ERROR ;
  }

  // Push instance and resource id on the stack and call the writeFunc
  lua_pushvalue(L, -2);  // stack: ..., instance, deleteFunc, instance
  lua_call(L, 1, 1); // stack: ..., instance, return_code

  // Get return code
  int ret = lua_tointeger(L, -1);

  // Clean the stack
  lua_pop(L, 2);
  return ret;
}

static uint8_t prv_create(uint16_t instanceId, int numData,
    lwm2m_data_t * dataArray, lwm2m_object_t * objectP) {
  // Get user data.
  luaobject_userdata * userdata = (luaobject_userdata*) objectP->userData;
  lua_State * L = userdata->L;

  // Get table of this object on the stack.
  lua_rawgeti(L, LUA_REGISTRYINDEX, userdata->tableref); // stack: ..., object

  // Get the create function
  lua_getfield(L, -1, "create"); // stack: ..., object, createFunc
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1); // clean the stack
    return COAP_500_INTERNAL_SERVER_ERROR ;
  }

  // Create instance in C list
  lwm2m_list_t * instance = (lwm2m_list_t *)c_zalloc(sizeof(lwm2m_list_t));
  if (NULL == instance)
    return COAP_500_INTERNAL_SERVER_ERROR;
  memset(instance, 0, sizeof(lwm2m_list_t));
  instance->id = instanceId;
  objectP->instanceList = LWM2M_LIST_ADD(objectP->instanceList, instance)

  // Push object and instance id on the stack and call the create function
  lua_pushvalue(L, -2);  // stack: ..., object, createFunc, object
  lua_pushinteger(L, instanceId); // stack: ..., object, createFunc, object, instanceId
  lua_call(L, 2, 2); // stack: ..., object, return_code, instance

  // Get return code
  int ret = lua_tointeger(L, -2);
  if (ret == COAP_201_CREATED) {
    // write value
    ret = prv_write(instanceId, numData, dataArray, objectP);
    if (ret == COAP_204_CHANGED) {
      return COAP_201_CREATED ;
    } else {
      prv_delete(instanceId, objectP);
      return ret;
    }
  }
  return ret;
}

static void prv_close(lwm2m_object_t * objectP) {

  luaobject_userdata * userdata = (luaobject_userdata *) objectP->userData;
  if (userdata != NULL) {
    // Release table reference in lua registry.
    if (userdata->tableref != LUA_NOREF) {
      luaL_unref(userdata->L, LUA_REGISTRYINDEX, userdata->tableref);
      userdata->tableref = LUA_NOREF;
    }

    // Release memory.
    c_free(userdata);
    objectP->userData = NULL;
  }
}

lwm2m_object_t * get_lua_object(lua_State *L, int tableindex, int objId) {

  // Allocate memory for lwm2m object.
  lwm2m_object_t * objectP = (lwm2m_object_t *) c_zalloc(
      sizeof(lwm2m_object_t));

  if (NULL != objectP) {
    c_memset(objectP, 0, sizeof(lwm2m_object_t));

    // Allocate memory for userdata.
    luaobject_userdata * userdata = (luaobject_userdata *) c_zalloc(
        sizeof(luaobject_userdata));
    if (userdata == NULL) {
      c_free(objectP);
      return NULL;
    }

    // store table represent this object
    lua_pushvalue(L, tableindex); // stack: ..., objectTable
    userdata->tableref = luaL_ref(L, LUA_REGISTRYINDEX); //stack: ...

    // set fields
    userdata->L = L;
    objectP->objID = objId;
    objectP->readFunc = prv_read;
    objectP->writeFunc = prv_write;
    objectP->executeFunc = prv_execute;
    objectP->createFunc = prv_create;
    objectP->deleteFunc = prv_delete;
    objectP->closeFunc = prv_close;
    objectP->userData = userdata;

    // Update instance List
    // ---------------------
    // Get table of this object on the stack.
    lua_rawgeti(L, LUA_REGISTRYINDEX, userdata->tableref); // stack: ..., objectTable
    int i = 0;
    lua_pushnil(L); // stack: ..., objectTable, key(nil)
    while (lua_next(L, -2) != 0) { // stack: ..., objectTable, key, value
      if (lua_isnumber(L, -2)) {
        int instanceid = lua_tointeger(L, -2);
        lwm2m_list_t * instance = (lwm2m_list_t *)c_zalloc(sizeof(lwm2m_list_t));
        if (NULL == instance)
          return NULL;
        c_memset(instance, 0, sizeof(lwm2m_list_t));
        instance->id = instanceid;
        objectP->instanceList = LWM2M_LIST_ADD(objectP->instanceList,
            instance);
      }
      // Removes 'value'; keeps 'key' for next iteration
      lua_pop(L, 1); // stack: ..., objectTable, key
    }
    // Clean the stack
    lua_pop(L, 1);

  }

  return objectP;
}

/*
 * lwm2m lua module
 */
void stackdump_g(lua_State* l) {
  int i;
  int top = lua_gettop(l);

  LWM2M_PRINTF(" ============== >total in stack %d\n", top);

  for (i = 1; i <= top; i++) { /* repeat for each level */
    int t = lua_type(l, i);
    switch (t) {
    case LUA_TSTRING: /* strings */
      LWM2M_PRINTF("string: '%s'\n", lua_tostring(l, i));
      break;
    case LUA_TBOOLEAN: /* booleans */
      LWM2M_PRINTF("boolean %s\n", lua_toboolean(l, i) ? "true" : "false");
      break;
    case LUA_TNUMBER: /* numbers */
      LWM2M_PRINTF("number: %g\n", lua_tointeger(l, i));
      break;
    default: /* other values */
      LWM2M_PRINTF("%s\n", lua_typename(l, t));
      break;
    }
    LWM2M_PRINTF("  "); /* put a separator */
  }
  LWM2M_PRINTF("\n"); /* end the listing */
}

typedef struct llwm_addr_t {
  char * host;
  int port;
} llwm_addr_t;

typedef struct llwm_userdata {
  lua_State * L;
  lwm2m_context_t * ctx;
  int sendCallbackRef;
  int connectServerCallbackRef;
} llwm_userdata;

static llwm_userdata * checkllwm(lua_State * L, const char * functionname) {
  llwm_userdata* lwu = (llwm_userdata*) luaL_checkudata(L, 1,
      "lualwm2m.llwm");

  if (lwu->ctx == NULL)
    luaL_error(L, "bad argument #1 to '%s' (llwm object is closed)",
        functionname);

  return lwu;
}

static uint8_t prv_buffer_send_callback(void * sessionH, uint8_t * buffer,
    size_t length, void * userData) {

  llwm_userdata * ud = userData;
  lua_State * L = ud->L;
  llwm_addr_t * la = (llwm_addr_t *) sessionH;

  lua_rawgeti(L, LUA_REGISTRYINDEX, ud->sendCallbackRef);
  lua_pushlstring(L, buffer, length);
  lua_pushstring(L, la->host);
  lua_pushnumber(L, la->port);
  lua_call(L, 3, 0);

  return COAP_NO_ERROR ;
}

static void * prv_connect_server_callback(uint16_t serverID, void * userData) {
  llwm_userdata * ud = userData;
  lua_State * L = ud->L;

  lua_rawgeti(L, LUA_REGISTRYINDEX, ud->connectServerCallbackRef);
  lua_pushnumber(L, serverID);
  lua_call(L, 1, 2);

  // Get server address.
  const char* host = lua_tostring(L, -2);
  int port = lua_tointeger(L, -1);
  lua_pop(L, 2); // clean the stack

  size_t lal = sizeof(struct llwm_addr_t);
  struct llwm_addr_t * la = (struct llwm_addr_t *)c_zalloc(lal);
  if (la == NULL)
    luaL_error(L, "Memory allocation problem when 'prv_connect_server_callback'");
  la->host = (char *)strdup(host);
  la->port = port;

  return la;
}

static int llwm_init(lua_State *L) {
  // 1st parameter : should be end point name.
  const char * endpointName = luaL_checkstring(L, 1);

  // 2nd parameter : should be a list of "lwm2m objects".
  luaL_checktype(L, 2, LUA_TTABLE);
  size_t objListLen = lua_objlen(L, 2);
  if (objListLen <= 0)
    return luaL_error(L,
        "bad argument #2 to 'init' (should be a non empty list : #table > 0)");

  // 3rd parameter : should be a callback.
  luaL_checktype(L, 3, LUA_TFUNCTION);

  // 4rd parameter : should be a callback.
  luaL_checktype(L, 4, LUA_TFUNCTION);

  // Create llwm userdata object and set its metatable.
  llwm_userdata * lwu = lua_newuserdata(L, sizeof(llwm_userdata)); // stack: endpoint, tableobj, connectcallback, sendcallback, lwu
  lwu->L = L;
  lwu->sendCallbackRef = LUA_NOREF;
  lwu->connectServerCallbackRef = LUA_NOREF;
  lwu->ctx = NULL;
  luaL_getmetatable(L, "lualwm2m.llwm"); // stack: endpoint, tableobj, connectcallback, sendcallback, lwu, metatable
  lua_setmetatable(L, -2); // stack: endpoint, tableobj, connectcallback, sendcallback, lwu
  lua_replace(L, 1); // stack: lwu, tableobj, connectcallback, sendcallback

  // Store callbacks in Lua registry to keep a reference on it.
  int sendCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX); // stack: lwu, tableobj, connectcallback
  int connectServerCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX); // stack: lwu, tableobj

  // Manage "lwm2m objects" list :
  // For each object in "lwm2m objects" list, create a "C lwm2m object".
  lwm2m_object_t * objArray[objListLen];
  int i;
  for (i = 1; i <= objListLen; i++) {
    // Get object table.
    lua_rawgeti(L, -1, i); // stack: lwu, tableobj, tableobj[i]
    if (lua_type(L, -1) != LUA_TTABLE)
      return luaL_error(L,
          "bad argument #2 to 'init' (all element of the list should be a table with a 'id' field which is a number )");

    // Check the id field is here.
    lua_getfield(L, -1, "id"); // stack: lwu, tableobj, tableobj[i], tableobj[i].id
    if (!lua_isnumber(L, -1))
      return luaL_error(L,
          "bad argument #2 to 'init' (all element of the list should be a table with a 'id' field which is a number)");

    int id = (int) lua_tointeger(L, -1);
    lua_pop(L, 1); // stack: lwu, tableobj, tableobj[i]

    // Create Lua Object.
    lwm2m_object_t * obj = get_lua_object(L, -1, id); //stack should not be modify by "get_lua_object".
    if (obj == NULL) {
      // object can not be create, release previous one.
      for (i--; i >= 1; i--) {
        c_free(objArray[i - 1]);
      }
      return luaL_error(L,
          "unable to create objects (Bad object structure or memory allocation problem ?)");
    }
    objArray[i - 1] = obj;
    lua_pop(L, 1); // stack: lwu, tableobj
  }
  lua_pop(L, 1); // stack: lwu

  // Context Initialization.
  lwm2m_context_t * contextP = lwm2m_init(prv_connect_server_callback,
      prv_buffer_send_callback, lwu);
  lwu->ctx = contextP;
  lwu->sendCallbackRef = sendCallbackRef;
  lwu->connectServerCallbackRef = connectServerCallbackRef;

  int res =  lwm2m_configure(contextP, endpointName, NULL, NULL, objListLen,
      objArray);
  if (res != COAP_NO_ERROR){
    luaL_error(L,
      "unable to initialize lwM2m context : configure failed (Bad object structure or memory allocation problem ?)");
  }
  return 1;
}

static int llwm_start(lua_State *L) {
  // Get llwm userdata.
  llwm_userdata * lwu = checkllwm(L, "start");

  // Start connection
  lwm2m_start(lwu->ctx);

  return 0;
}

static int llwm_handle(lua_State *L) {
  // Get llwm userdata.
  llwm_userdata * lwu = checkllwm(L, "handle");

  // Get data buffer.
  size_t length;
  uint8_t * buffer = (uint8_t*) luaL_checklstring(L, 2, &length);

  // Get server address.
  const char* host = luaL_checkstring(L, 3);
  int port = luaL_checkint(L, 4);

  // HACK : https://github.com/01org/liblwm2m/pull/18#issuecomment-45501037
  // find session object in the server list.
  lwm2m_server_t * targetP;
  llwm_addr_t * la = NULL;
  bool found = false;
  targetP = lwu->ctx->serverList;
  while (targetP != NULL && !found) {
    // get host and port of the target
    llwm_addr_t * session = (llwm_addr_t *) targetP->sessionH;
    if (session != NULL && session->port == port
        && c_strcmp(session->host, host) == 0) {
      la = session;
      found = true;
    } else {
      targetP = targetP->next;
    }
  }

  // Handle packet
  if (found)
    lwm2m_handle_packet(lwu->ctx, buffer, length, la);

  return 0;
}

static int llwm_step(lua_State *L) {
  // Get llwm userdata.
  llwm_userdata * lwu = checkllwm(L, "step");

  // TODO make this arguments available in lua.
  time_t tv_sec;
  tv_sec = 60;
  lwm2m_step(lwu->ctx, &tv_sec);

  return 0;
}

static int llwm_resource_changed(lua_State *L) {
  // Get llwm userdata.
  llwm_userdata *lwu = checkllwm(L, "resource_changed");

  // Get parameters.
  size_t length;
  uint8_t * uriPath = (uint8_t*) luaL_checklstring(L, 2, &length);

  // Create URI of resource which changed.
  lwm2m_uri_t uri;
  int result = lwm2m_stringToUri(uriPath, length, &uri);
  if (result == 0) {
    lua_pushnil(L);
    lua_pushstring(L, "resource uri syntax error");
    return 2;
  }

  //notify the change.
  lwm2m_resource_value_changed(lwu->ctx, &uri);
  return 0;
}

static int llwm_close(lua_State *L) {
  // Get llwm userdata
  llwm_userdata* lwu = (llwm_userdata*) luaL_checkudata(L, 1,
      "lualwm2m.llwm");

  // Close lwm2m context.
  if (lwu->ctx) {
    lwm2m_close(lwu->ctx);
    lwu->ctx->userData = NULL;
  }

  // Release callbacks.
  luaL_unref(L, LUA_REGISTRYINDEX, lwu->sendCallbackRef);
  lwu->sendCallbackRef = LUA_NOREF;
  luaL_unref(L, LUA_REGISTRYINDEX, lwu->connectServerCallbackRef);
  lwu->connectServerCallbackRef = LUA_NOREF;

  lwu->ctx = NULL;

  return 0;
}

// Module function map
#define MIN_OPT_LEVEL 2
#include "lrodefs.h"

static const LUA_REG_TYPE llwm_objmeths[] = { 
  //{ LSTRKEY( "handle" ),  LFUNCVAL ( llwm_handle ) }, 
  //{ LSTRKEY( "start" ),   LFUNCVAL ( llwm_start ) }, 
  //{ LSTRKEY( "close" ),   LFUNCVAL ( llwm_close ) }, 
  //{ LSTRKEY( "step" ),    LFUNCVAL ( llwm_step ) }, 
  //{ LSTRKEY( "resourcechanged" ), LFUNCVAL ( llwm_resource_changed ) }, 
  //{ LSTRKEY( "__gc" ),    LFUNCVAL ( llwm_close ) }, 
  { LNILKEY, LNILVAL }
};

const LUA_REG_TYPE llwm_modulefuncs[] = { 
  { LSTRKEY( "init" ), LFUNCVAL ( llwm_init ) }, 
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_lwm2m( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  luaL_rometatable(L, "lwm2m_client", (void *)llwm_objmeths);  // create metatable for coap_client  
  return 0;
#else // #if LUA_OPTIMIZE_MEMORY > 0
  int n;
  luaL_register( L, AUXLIB_LWM2M, llwm_modulefuncs );

  // Set it as its own metatable
  lua_pushvalue( L, -1 );
  lua_setmetatable( L, -2 );

  n = lua_gettop(L);

  lua_settop(L, n);
  // create metatable
  luaL_newmetatable(L, "lwm2m_client");
  // metatable.__index = metatable
  lua_pushliteral(L, "__index");
  lua_pushvalue(L,-2);
  lua_rawset(L,-3);
  // Setup the methods inside metatable
  luaL_register( L, NULL, llwm_objmeths );

  return 1;
#endif // #if LUA_OPTIMIZE_MEMORY > 0  
}
