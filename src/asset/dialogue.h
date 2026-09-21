/* `udlg` (dialogue): what a character says, including what it says dying.
 *
 * Halo keeps every line one unit can speak in a single tag of 90-odd
 * TagDependencies, one per situation -- pain, fear, a kill, a killing spree,
 * a friend dying. Dialogue reconciles at 4112, which is the check that the
 * slots below land where they should.
 *
 * Blood Gulch carries exactly ONE of these, `sound\dialog\chief\chief`, so
 * finding it by class is unambiguous rather than a guess. A campaign map
 * carries one per character and would want the actor variant instead.
 */
#ifndef HTA_DIALOGUE_H
#define HTA_DIALOGUE_H

#include "cache.h"

/* Offsets into the Dialogue tag. Every one is a TagDependency, so the `snd!`
 * id is at +12 as usual. Only the ones a lone player can actually cause are
 * listed: the rest need someone else in the world. */
#define HTA_DLG_PAIN_BODY_MINOR  112u
#define HTA_DLG_PAIN_BODY_MAJOR  128u
#define HTA_DLG_PAIN_SHIELD      144u
#define HTA_DLG_PAIN_FALLING     160u
#define HTA_DLG_SCREAM_FEAR      176u
#define HTA_DLG_SCREAM_PAIN      192u
#define HTA_DLG_DEATH_QUIET      240u
#define HTA_DLG_DEATH_VIOLENT    256u
#define HTA_DLG_DEATH_FALLING    272u
#define HTA_DLG_DEATH_AGONIZING  288u
#define HTA_DLG_DEATH_INSTANT    304u

/* The map's dialogue tag, or 0. */
uint32_t hta_dialogue_tag(const hta_cache *c);

/* One line out of it: pass an HTA_DLG_* offset. 0 when that slot is empty,
 * which is common and not a failure. */
uint32_t hta_dialogue_sound(const hta_cache *c, uint32_t udlg_tag_id,
                            uint32_t slot);

#endif
