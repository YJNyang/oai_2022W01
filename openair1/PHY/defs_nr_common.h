/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file PHY/defs_nr_common.h
 \brief Top-level defines and structure definitions
 \author Guy De Souza
 \date 2018
 \version 0.1
 \company Eurecom
 \email: desouza@eurecom.fr
 \note
 \warning
*/

#ifndef __PHY_DEFS_NR_COMMON__H__
#define __PHY_DEFS_NR_COMMON__H__

#include "defs_common.h"
#include "nfapi_nr_interface.h"
#include "impl_defs_nr.h"
#include "PHY/CODING/nrPolar_tools/nr_polar_defs.h"

#define nr_subframe_t lte_subframe_t

#define MAX_NUM_SUBCARRIER_SPACING 5

#define NR_MAX_NB_RB 275

#define NR_NB_SC_PER_RB 12
#define NR_NB_REG_PER_CCE 6

#define NR_SYMBOLS_PER_SLOT 14

#define ONE_OVER_SQRT2_Q15 23170
#define ONE_OVER_TWO_Q15 16384

#define NR_MOD_TABLE_SIZE_SHORT 686
#define NR_MOD_TABLE_BPSK_OFFSET 1
#define NR_MOD_TABLE_QPSK_OFFSET 3
#define NR_MOD_TABLE_QAM16_OFFSET 7
#define NR_MOD_TABLE_QAM64_OFFSET 23
#define NR_MOD_TABLE_QAM256_OFFSET 87

#define NR_PSS_LENGTH 127
#define NR_SSS_LENGTH 127

#define NR_PBCH_DMRS_LENGTH 144 // in mod symbols
#define NR_PBCH_DMRS_LENGTH_DWORD 10 // ceil(2(QPSK)*NR_PBCH_DMRS_LENGTH/32)

/*These max values are for the gold sequences which are generated at init for the
 * full carrier bandwidth*/
#define NR_MAX_PDCCH_DMRS_INIT_LENGTH ((NR_MAX_NB_RB<<1)*3) // 3 symbols *2(QPSK)
#define NR_MAX_PDCCH_DMRS_INIT_LENGTH_DWORD 52 // ceil(NR_MAX_PDCCH_DMRS_LENGTH/32)
/*used for the resource mapping*/
#define NR_MAX_PDCCH_DMRS_LENGTH 576 // 16(L)*2(QPSK)*3(3 DMRS symbs per REG)*6(REG per CCE)

#define NR_MAX_PDSCH_DMRS_LENGTH 1100      //275*2(k)*2(QPSK)
#define NR_MAX_PDSCH_DMRS_INIT_LENGTH_DWORD 35  // ceil(NR_MAX_PDSCH_DMRS_LENGTH/32)

#define NR_MAX_DCI_PAYLOAD_SIZE 64
#define NR_MAX_DCI_SIZE 1728 //16(L)*2(QPSK)*9(12 RE per REG - 3(DMRS))*6(REG per CCE)
#define NR_MAX_DCI_SIZE_DWORD 54 // ceil(NR_MAX_DCI_SIZE/32)

#define NR_MAX_NUM_BWP 4

#define NR_MAX_PDCCH_AGG_LEVEL 16
#define NR_MAX_CSET_DURATION 3

#define NR_MAX_NB_RBG 18
#define NR_MAX_NB_LAYERS 8

typedef enum {
  NR_MU_0=0,
  NR_MU_1,
  NR_MU_2,
  NR_MU_3,
  NR_MU_4,
} nr_numerology_index_e;

typedef enum {
  kHz15=0,
  kHz30,
  kHz60,
  kHz120,
  kHz240
} nr_scs_e;

typedef enum{
  nr_ssb_type_A = 0,
  nr_ssb_type_B,
  nr_ssb_type_C,
  nr_ssb_type_D,
  nr_ssb_type_E
} nr_ssb_type_e;

typedef enum {
  nr_FR1 = 0,
  nr_FR2
} nr_frequency_range_e;

typedef struct {
  /// Size of first RBG
  uint8_t start_size;
  /// Nominal size
  uint8_t P;
  /// Size of last RBG
  uint8_t end_size;
  /// Number of RBG
  uint8_t N_RBG;
}nr_rbg_parms_t;

typedef struct {
  /// Size of first PRG
  uint8_t start_size;
  /// Nominal size
  uint8_t P_prime;
  /// Size of last PRG
  uint8_t end_size;
  /// Number of PRG
  uint8_t N_PRG;
} nr_prg_parms_t;

typedef struct NR_BWP_PARMS {
  /// BWP ID
  uint8_t bwp_id;
  /// Subcarrier spacing
  nr_scs_e scs;
  /// Freq domain location -- 1st CRB index
  uint8_t location;
  /// Bandwidth in PRB
  uint16_t N_RB;
  /// Size of FFT/IFFT
  uint16_t ofdm_symbol_size;
  /// Cyclic prefix
  uint8_t cyclic_prefix;
  /// RBG params
  nr_rbg_parms_t rbg_parms;
  /// PRG params
  nr_prg_parms_t prg_parms;
} NR_BWP_PARMS;

typedef struct {
  uint8_t reg_idx;
  uint16_t start_sc_idx;
  uint8_t symb_idx;
} nr_reg_t;

typedef struct {
  uint8_t cce_idx;
  nr_reg_t reg_list[NR_NB_REG_PER_CCE];
} nr_cce_t;

typedef struct NR_DL_FRAME_PARMS {
  /// frequency range
  nr_frequency_range_e freq_range;
  /// Number of resource blocks (RB) in DL
  uint8_t N_RB_DL;
  /// Number of resource blocks (RB) in UL
  uint8_t N_RB_UL;
  ///  total Number of Resource Block Groups: this is ceil(N_PRB/P)
  uint8_t N_RBG;
  /// Total Number of Resource Block Groups SubSets: this is P
  uint8_t N_RBGS;
  /// EUTRA Band
  uint8_t eutra_band;
  /// DL carrier frequency
  uint32_t dl_CarrierFreq;
  /// UL carrier frequency
  uint32_t ul_CarrierFreq;
  /// TX attenuation
  uint32_t att_tx;
  /// RX attenuation
  uint32_t att_rx;
  ///  total Number of Resource Block Groups: this is ceil(N_PRB/P)
  /// Frame type (0 FDD, 1 TDD)
  lte_frame_type_t frame_type;
  /// TDD subframe assignment (0-7) (default = 3) (254=RX only, 255=TX only)
  uint8_t tdd_config;
  /// TDD S-subframe configuration (0-9)
  /// Cell ID
  uint16_t Nid_cell;
  uint32_t subcarrier_spacing;
  /// 3/4 sampling
  uint8_t threequarter_fs;
  /// Size of FFT
  uint16_t ofdm_symbol_size;
  /// Number of prefix samples in all but first symbol of slot
  uint16_t nb_prefix_samples;
  /// Number of prefix samples in first symbol of slot
  uint16_t nb_prefix_samples0;
  /// Carrier offset in FFT buffer for first RE in PRB0
  uint16_t first_carrier_offset;
  /// Number of OFDM/SC-FDMA symbols in one slot
  uint16_t symbols_per_slot;
  /// Number of slots per subframe
  uint16_t slots_per_subframe;
    /// Number of slots per frame
  uint16_t slots_per_frame;
  /// Number of samples in a subframe
  uint32_t samples_per_subframe;
  /// Number of OFDM/SC-FDMA symbols in one subframe (to be modified to account for potential different in UL/DL)
  uint16_t symbols_per_tti;
  /// Number of samples in a radio frame
  uint32_t samples_per_frame;
  /// Number of samples in a subframe without CP
  uint32_t samples_per_subframe_wCP;
  /// Number of samples in a radio frame without CP
  uint32_t samples_per_frame_wCP;
  /// Number of samples in a tti (same as subrame in LTE, depending on numerology in NR)
  uint32_t samples_per_tti;
  /// NR numerology index [0..5] as specified in 38.211 Section 4 (mu). 0=15khZ SCS, 1=30khZ, 2=60kHz, etc
  uint8_t numerology_index;
  /// NR number of ttis per subframe deduced from numerology (cf 38.211): 1, 2, 4, 8(not supported),16(not supported),32(not supported)
  uint8_t ttis_per_subframe;
  /// NR number of slots per tti . Assumption only 2 Slot per TTI is supported (Slot Config 1 in 38.211)
  uint8_t slots_per_tti;
  /// Number of Physical transmit antennas in node
  uint8_t nb_antennas_tx;
  /// Number of Receive antennas in node
  uint8_t nb_antennas_rx;
  /// Number of common transmit antenna ports in eNodeB (1 or 2)
  uint8_t nb_antenna_ports_eNB;
  /// Cyclic Prefix for DL (0=Normal CP, 1=Extended CP)
  lte_prefix_type_t Ncp;
  /// shift of pilot position in one RB
  uint8_t nushift;
  /// SRS configuration from TS 38.331 RRC
  SRS_NR srs_nr;

  /// for NR TDD management
  TDD_UL_DL_configCommon_t  *p_tdd_UL_DL_Configuration;

  TDD_UL_DL_configCommon_t  *p_tdd_UL_DL_ConfigurationCommon2;

  TDD_UL_DL_SlotConfig_t    *p_TDD_UL_DL_ConfigDedicated;

  /// TDD configuration
  uint16_t tdd_uplink_nr[MAX_NR_OF_SLOTS]; /* this is a bitmap of symbol of each slot given for 2 frames */

   //SSB related params
  /// Start in Subcarrier index of the SSB block
  uint16_t ssb_start_subcarrier;
  /// SSB type
  nr_ssb_type_e ssb_type;
  /// PBCH polar encoder params
  t_nrPolar_params pbch_polar_params;

   //BWP params
  NR_BWP_PARMS initial_bwp_dl;
  NR_BWP_PARMS initial_bwp_ul;

} NR_DL_FRAME_PARMS;


#endif
