#ifndef MM_APP_SURVIVAL_H
#define MM_APP_SURVIVAL_H
#include "session.h"
bool hta_survival_begin(hta_session *s);
bool hta_survival_join(hta_session *s,int unit,const uint8_t identity[16],bool solo);
void hta_survival_leave(hta_session *s,int unit);
void hta_survival_tick(hta_session *s,float dt);
void hta_survival_event(hta_session *s,const hta_game_event *e);
bool hta_survival_cast(hta_session *s,int unit);
uint64_t hta_survival_price(const hta_session *s,int unit,unsigned item);
uint8_t hta_survival_request(hta_session *s,int unit,uint8_t action,unsigned item,uint16_t serial);
void hta_survival_flush(hta_session *s);
void hta_survival_snapshot(const hta_session *s,int unit,hta_net_rpg *out);
void hta_survival_apply(hta_session *s,int unit,const hta_net_rpg *in);
void hta_survival_text(const hta_session *s,int unit,char *out,size_t n);
#endif
