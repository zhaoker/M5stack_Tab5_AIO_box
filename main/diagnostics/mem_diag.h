#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void mem_diag_checkpoint(const char *label);
void mem_diag_checkpoint_reset(void);

#ifdef __cplusplus
}
#endif
