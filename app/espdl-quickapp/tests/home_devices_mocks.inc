/* SPDX-License-Identifier: Apache-2.0
 * Offline transport/storage fixtures; production state machine is unchanged.
 */
static char mock_endpoint[192] = "http://homeassistant.local:8123";
static char mock_saved[1537],mock_entity[128];
static const char *mock_state = "on";
static unsigned mock_configuration = 1,mock_writes,mock_closes,mock_requests;
static bool mock_hold;
static int mock_pending,mock_write_error,mock_status=200;
static bool mock_busy;
int qpk_storage_read(const char *root,const char *package,const char *key,
                     char *value,size_t capacity,size_t *length)
{
  (void)root;assert(!strcmp(package,"com.openvela.homeassistant"));assert(!strcmp(key,"eh_desktop"));
  if(!mock_saved[0])return -ENOENT;
  *length=strlen(mock_saved);assert(capacity>*length);strcpy(value,mock_saved);return 0;
}
int qpk_storage_write(const char *root,const char *package,const char *key,
                      const char *value,size_t length)
{
  (void)root;assert(!strcmp(package,"com.openvela.homeassistant"));assert(!strcmp(key,"eh_desktop"));
  assert(length<sizeof(mock_saved));mock_writes++;if(mock_write_error)return mock_write_error;strcpy(mock_saved,value);return 0;
}
int hass_open(unsigned grants){assert(grants==HASS_READ);return 1;}
int hass_close(uint32_t client){assert(client==1);mock_closes++;mock_pending=0;return 0;}
int hass_get_url(uint32_t client,char *url,size_t size)
{assert(client==1&&strlen(mock_endpoint)<size);strcpy(url,mock_endpoint);return 0;}
uint32_t hass_configuration_revision(void){return mock_configuration;}
int hass_get_state(uint32_t client,const char *entity)
{assert(client==1);if(mock_busy)return -EBUSY;assert(!mock_pending);strcpy(mock_entity,entity);return mock_pending=++mock_requests;}
int hass_poll(uint32_t client,struct hass_result_s *result)
{
  assert(client==1);memset(result,0,sizeof(*result));
  if(mock_hold||!mock_pending)return 0;
  char json[320];snprintf(json,sizeof(json),"{\"entity_id\":\"%s\",\"state\":\"%s\"}",mock_entity,mock_state);
  result->body=strdup(json);assert(result->body);result->done=true;
  result->status=mock_status;result->request_id=mock_pending;mock_pending=0;return 0;
}
void hass_result_free(struct hass_result_s *result){free(result->body);}
