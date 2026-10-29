/**
  ******************************************************************************
  * @file    settings.c
  * @brief   Persistance flash. Le secteur 7 est exclu du linker
  *          (STM32F411RETX_FLASH.ld : FLASH LENGTH = 384K).
  *          Attention : pendant une ecriture/effacement flash le CPU est
  *          bloque (execution depuis la meme flash) -> on n'ecrit que si le
  *          codeur est a l'arret.
  ******************************************************************************
  */
#include "settings.h"
#include "encoder_sim.h"
#include "main.h"
#include <string.h>
#include <stddef.h>

#define SET_ADDR      0x08060000u
#define SET_SIZE      (128u * 1024u)
#define SET_SECTOR    FLASH_SECTOR_7
#define SET_MAGIC     0x53455432u      /* "SET2" */
#define REC_WORDS     16u              /* 64 octets */
#define SAVE_DELAY_MS 2000u            /* attend 2 s de stabilite avant d'ecrire */

typedef struct
{
  uint32_t   magic;
  Settings_t s;            /* 44 octets */
  uint32_t   seq;
  uint32_t   crc;
} Rec_t;
_Static_assert(sizeof(Rec_t) <= REC_WORDS * 4u, "record > 64 octets");

Settings_t g_set;
static Settings_t s_saved;
static uint32_t   s_next_off = 0;      /* offset du prochain slot libre */
static uint32_t   s_seq = 0;
static uint32_t   s_change_t = 0;
static bool       s_pending = false;
static bool       s_need_erase = false;

static const Settings_t s_defaults =
{
  .ppcm = 50.0f, .va_speed_mh = 300.0f, .va_depth_m = 1.0f, .jog_speed_mh = 200.0f, .theme = 0,
  .tcal_ok = 0, .tcal = {0},
};

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
  uint32_t c = 0xFFFFFFFFu;
  while (n--)
  {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
  }
  return ~c;
}

static bool valid(const Settings_t *s)
{
  return s->ppcm >= (float)ENC_PPCM_MIN && s->ppcm <= (float)ENC_PPCM_MAX &&
         s->va_speed_mh > 0.0f && s->va_speed_mh <= (float)ENC_VMAX_MH &&
         s->jog_speed_mh > 0.0f && s->jog_speed_mh <= (float)ENC_VMAX_MH &&
         s->va_depth_m > 0.0f && s->va_depth_m < 100000.0f && s->theme <= 1u && s->tcal_ok <= 1u;
}

static bool slot_erased(uint32_t off)
{
  const uint32_t *w = (const uint32_t *)(SET_ADDR + off);
  for (uint32_t i = 0; i < REC_WORDS; i++) if (w[i] != 0xFFFFFFFFu) return false;
  return true;
}

void SETTINGS_Load(void)
{
  const Rec_t *best = 0;
  int32_t last_used = -1;
  s_need_erase = false;

  for (uint32_t off = 0; off + REC_WORDS * 4u <= SET_SIZE; off += REC_WORDS * 4u)
  {
    if (slot_erased(off)) continue;
    last_used = (int32_t)(off / (REC_WORDS * 4u));
    const Rec_t *r = (const Rec_t *)(SET_ADDR + off);
    if (r->magic == SET_MAGIC &&
        r->crc == crc32((const uint8_t *)r, offsetof(Rec_t, crc)) &&
        valid(&r->s))
      best = r;
    else
      s_need_erase = true;                        /* ancien format ou donnee corrompue */
  }
  s_next_off = (uint32_t)(last_used + 1) * REC_WORDS * 4u;

  if (best) { g_set = best->s; s_seq = best->seq; }
  else      { g_set = s_defaults; s_seq = 0; }
  s_saved = g_set;
}

static void write_record(void)
{
  Rec_t r;
  memset(&r, 0xFF, sizeof r);
  r.magic = SET_MAGIC;
  r.s     = g_set;
  r.seq   = ++s_seq;
  r.crc   = crc32((const uint8_t *)&r, offsetof(Rec_t, crc));

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  if (s_need_erase || s_next_off + REC_WORDS * 4u > SET_SIZE) /* plein ou ancien format : efface */
  {
    FLASH_EraseInitTypeDef er = {0};
    uint32_t err;
    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.Sector       = SET_SECTOR;
    er.NbSectors    = 1;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&er, &err) != HAL_OK) { HAL_FLASH_Lock(); return; }
    s_next_off = 0;
    s_need_erase = false;
  }

  const uint32_t *w = (const uint32_t *)&r;
  uint32_t addr = SET_ADDR + s_next_off;
  for (uint32_t i = 0; i < REC_WORDS && i * 4u < sizeof r; i++)
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + 4u * i, w[i]) != HAL_OK) break;
  HAL_FLASH_Lock();

  s_next_off += REC_WORDS * 4u;
  s_saved = g_set;
}

static bool same(const Settings_t *a, const Settings_t *b)
{
  return a->ppcm == b->ppcm && a->va_speed_mh == b->va_speed_mh &&
         a->va_depth_m == b->va_depth_m && a->jog_speed_mh == b->jog_speed_mh &&
         a->theme == b->theme && a->tcal_ok == b->tcal_ok &&
         memcmp(a->tcal, b->tcal, sizeof a->tcal) == 0;
}

void SETTINGS_Task(void)
{
  g_set.ppcm = (float)ENC_GetPulsesPerCm();                  /* suit aussi la commande PPCM */

  if (!same(&g_set, &s_saved))
  {
    if (!s_pending) { s_pending = true; s_change_t = HAL_GetTick(); }
    if (valid(&g_set) && ENC_GetMode() == ENC_IDLE && (HAL_GetTick() - s_change_t) >= SAVE_DELAY_MS)
    {
      write_record();
      s_pending = false;
    }
  }
  else s_pending = false;
}
