#include <algorithm>
#include <array>

#include "common.h"
#include "SbpToNmea2kConverter.h"

// SBP_UTC_TIME, SBP_DOPS, SBP_POS_LLH 259, 520, 522 -> GNSS Position Data PGN 129029
bool SbpToNmea2kConverter::Sbp259And520And522ToPgn129029(const bool is_valid,
                                                         tN2kMsg *msg) {
  d << __FUNCTION__ << "\n";

  if(is_valid) {
    d << "\tSats used in solution: " << static_cast<u16>(sat_count_cache_)
      << "\n";
    // We hardcode reference station count to zero.
    // That's a lie when RTK is in use. We're happy with it, for now.
    SetN2kGNSS(*msg, tow_to_sid(tow_cache_), days_since_1970_cache_,
               seconds_since_midnight_cache_, lat_cache_, lon_cache_,
               alt_cache_, tN2kGNSStype::N2kGNSSt_GPSGLONASS,
               tN2kGNSSmethod::N2kGNSSm_PreciseGNSS, sat_count_cache_,
               hdop_cache_, pdop_cache_, /*GeoidalSeparation=*/N2kDoubleNA);

    // Have to do GNSS method manually. It's not reverse engineered yet.
    msg->Data[31] = gnss_metod_cache_;
    // Have to set integrity manually. 6 bits reserved. 2 bits set to 0.
    // That's why there are two values binary ORed together.
    // 0: No integrity checking.
    msg->Data[32] = 0xFC | 0x00;
  } else {
    SetN2kGNSS(*msg, /*sequence ID=*/0xFF,
            /*DaysSince1970=*/N2kUInt16NA,
            /*SecondsSinceMidnight=*/N2kDoubleNA,
            /*Latitude=*/N2kDoubleNA, /*Longitude=*/N2kDoubleNA,
            /*Altitude=*/N2kDoubleNA, tN2kGNSStype::N2kGNSSt_GPSGLONASS,
               tN2kGNSSmethod::N2kGNSSm_PreciseGNSS,
            /*nSatellites=*/N2kUInt8NA, /*HDOP=*/N2kDoubleNA,
            /*PDOP=*/N2kDoubleNA, /*GeoidalSeparation=*/N2kDoubleNA);
    // Have to do GNSS method manually. It's not reverse engineered yet.
    msg->Data[31] = gnss_metod_cache_;
    // Have to set integrity manually. 6 bits reserved. 2 bits set to 0.
    // That's why there are two values binary ORed together.
    // 0: No integrity checking.
    msg->Data[32] = 0xFC | 0x00;
  }

  d << "\tDone.\n";

  return true;
}

// SBP_DOPS 520 -> GNSS DOPs PGN 129539
bool SbpToNmea2kConverter::Sbp520ToPgn129539(const msg_dops_t *msg,
                                             tN2kMsg *n2kMsg) {
  d << __FUNCTION__ << "\n";

  bool is_valid = (msg->flags & 0x07) != 0;

  if (is_valid) {
    SetN2kGNSSDOPData(*n2kMsg, tow_to_sid(msg->tow),
            /*DesiredMode=*/tN2kGNSSDOPmode::N2kGNSSdm_Auto,
            /*ActualMode=*/tN2kGNSSDOPmode::N2kGNSSdm_3D,
                      msg->hdop / 100.0, msg->vdop / 100.0, msg->tdop / 100.0);
  } else {
    SetN2kGNSSDOPData(*n2kMsg, /*sequence ID=*/0xFF,
            /*DesiredMode=*/tN2kGNSSDOPmode::N2kGNSSdm_Auto,
            /*ActualMode=*/tN2kGNSSDOPmode::N2kGNSSdm_Error,
                      N2kDoubleNA, N2kDoubleNA, N2kDoubleNA);
  }

  d << "\tTOW: " << msg->tow << "\n"
    << "\tHDOP: " << msg->hdop / 100.0 << "\n"
    << "\tVDOP: " << msg->vdop / 100.0 << "\n"
    << "\tTDOP: " << msg->tdop / 100.0 << "\n";

  d << "\tDone.\n";

  return true;
}

// SBP_TRACKING_STATE 65 -> GNSS Sats in View PGN 129540
bool  SbpToNmea2kConverter::Sbp65ToPgn129540(const msg_tracking_state_t *msg,
                                             const u8 len, tN2kMsg *n2kMsg) {

  // Divide by the divisor. Very hacky to have it in a static.
  // TODO(lstrz): Make this non-static.
  constexpr u8 cDivider = 2;
  static u8 divider_counter = 0;
  if ((++divider_counter % cDivider) != 0) {
    return false;
  }

  d << __FUNCTION__ << "\n"
    << "\tGot " << static_cast<u16>(len) << "/"
    << sizeof(tracking_channel_state_t) << "="
    << static_cast<u16>(len / sizeof(tracking_channel_state_t))
    << " states. Message len is most likely statically allocated.\n";

  constexpr u8 cConstellationMaxSats = 32;
  std::array<std::pair<u8, u8>, cConstellationMaxSats> sats_gps = {};
  std::array<std::pair<u8, u8>, cConstellationMaxSats> sats_glo = {};
  auto sats_gps_end_it = sats_gps.begin();
  auto sats_glo_end_it = sats_glo.begin();
  for(u8 i = 0; i < (len / sizeof(tracking_channel_state_t)); ++i) {
    auto tracking_channel_state = msg->states[i];
    if(tracking_channel_state.sid.sat == 0) {  // Invalid sat.
      continue;
    }

    switch (tracking_channel_state.sid.code) {
      case 0:  // GPS satellites. Fallthrough.
      case 1:
      case 2:
      case 5:
      case 6:
        *sats_gps_end_it = std::make_pair(tracking_channel_state.sid.sat,
                                          tracking_channel_state.cn0);
        ++sats_gps_end_it;
        break;
      case 3:  // GLONASS satellites. Fallthrough.
      case 4:
        *sats_glo_end_it = std::make_pair(tracking_channel_state.sid.sat,
                                          tracking_channel_state.cn0);
        ++sats_glo_end_it;
        break;
      default:
        d << "\tControl should have never reached this point.\n";
        exit(-1);  // Should never happen.
    }
  }

  // Sort and remove duplicates.
  auto comparator_lower_than = [](const std::pair<u8, u8>& a,
                                  const std::pair<u8, u8>& b) {
      // First is the satellite ID, second is the CN0.
      return a.first < b.first ? true : a.second > b.second;
  };
  auto comparator_equal = [](const std::pair<u8, u8>& a,
                             const std::pair<u8, u8>& b) {
      return a.first == b.first;
  };
  std::sort(sats_gps.begin(), sats_gps_end_it, comparator_lower_than);
  std::sort(sats_glo.begin(), sats_glo_end_it, comparator_lower_than);
  sats_gps_end_it = std::unique(sats_gps.begin(), sats_gps_end_it,
                                comparator_equal);
  sats_glo_end_it = std::unique(sats_glo.begin(), sats_glo_end_it,
                                comparator_equal);

  // Triton2 screen that this was tested with acted weird in regards
  // to sat count:
  //  - up to 12 sats (inclusive), everything was displayed fine
  //  - from 13 to 17 sats (inclusive), number 12 would be shown on screen
  //  - from 18 and upwards, no sat count number would be shown
  // The problem has to do with the number of detailed sat infos actually
  // sent, not just the count that was sent.
  // This is the number of tracked satellites.
  u8 sat_count = sats_gps_end_it - sats_gps.begin() +
                 sats_glo_end_it - sats_glo.begin();
  d << "\tDeduplicated. " << static_cast<u16>(sat_count)
    << " sats left. GPS: " << sats_gps_end_it - sats_gps.begin()
    << " GLONASS: " << sats_glo_end_it - sats_glo.begin() << "\n";

  n2kMsg->SetPGN(129540L);
  n2kMsg->Priority=6;
  // This sequence ID is a guess. Have no tow to get a definite one.
  n2kMsg->AddByte(last_sid);
  // TODO: At present, we can't get range residuals.
  n2kMsg->AddByte(0xFF);
  n2kMsg->AddByte(sat_count);
  for(auto it = sats_gps.begin(); it != sats_gps_end_it; ++it) {
    n2kMsg->AddByte(/*PRN=*/it->first);
    // TODO: At present, we can't get azimuth and elevation.
    n2kMsg->Add2ByteUDouble(/*elevation=*/N2kDoubleNA, 0.0001, N2kDoubleNA);
    n2kMsg->Add2ByteUDouble(/*azimuth=*/N2kDoubleNA, 0.0001, N2kDoubleNA);
    n2kMsg->Add2ByteUDouble(/*CN0=*/it->second * 0.25, 0.01, N2kDoubleNA);
    // TODO: At present, we can't get range residuals.
    n2kMsg->Add4ByteUInt(/*rangeResiduals=*/N2kInt32NA);
    // TODO: At present, we can't tel if the satellite is used in the solution.
    // Byte contains two 4-bit fields. One is reserved. Other is PRN status.
    // 1: Tracked but not used in solution.
    n2kMsg->AddByte(/*tracked=*/0xF1);
  }
  for(auto it = sats_glo.begin(); it != sats_glo_end_it; ++it) {
    n2kMsg->AddByte(/*PRN=*/it->first + 64);
    // TODO: At present, we can't get azimuth and elevation.
    // Stefan: not in SBP, but see NMEA 183 message GSV implementation,
    // we have (should have) some there I think
    n2kMsg->Add2ByteUDouble(/*elevation=*/N2kDoubleNA, 0.0001, N2kDoubleNA);
    n2kMsg->Add2ByteUDouble(/*azimuth=*/N2kDoubleNA, 0.0001, N2kDoubleNA);
    n2kMsg->Add2ByteUDouble(/*CN0=*/it->second * 0.25, 0.01, N2kDoubleNA);
    // TODO: At present, we can't get range residuals.
    n2kMsg->Add4ByteUInt(/*rangeResiduals=*/N2kInt32NA);
    // TODO: At present, we can't tel if the satellite is used in the solution.
    // Byte contains two 4-bit fields. One is reserved. Other is PRN status.
    // 1: Tracked but not used in solution.
    n2kMsg->AddByte(/*tracked=*/0xF1);
  }
  d << "\tDone.\n";

  return true;
}

void SbpToNmea2kConverter::ResetPgn129029CacheIfOld(const u32 tow) {
  constexpr u32 cOverflowDiff = 1000 * 60 * 60;  // One hour in ms.
  if((tow > tow_cache_) ||  // Check if new tow or if overflown.
     ((tow < tow_cache_) && ((tow_cache_ - tow) <= cOverflowDiff))) {
    tow_cache_ = tow;
    ResetPgn129029Cache();
  }
}

bool SbpToNmea2kConverter::IsPgn129029Ready(const bool is_valid) {
      // Checking ((tow_cache_ % 10000) / 100) limits divider to two digits.
      if(is_valid) {
        divider_pgn129029_counter_ = ((tow_cache_ % 10000) / 100) %
                cDividerPgn129029;
        return utc_time_cached_ && pos_llh_cached_ && dops_cached_ &&
                divider_pgn129029_counter_ == 0;
      } else {  // This should be hit only by a single SBP message.
        divider_pgn129029_counter_ = (divider_pgn129029_counter_ + 1) %
                cDividerPgn129029;
        return divider_pgn129029_counter_ == 0;
      }
    }

void
SbpToNmea2kConverter::SetPgn129029CacheFields(const u32 tow,
                                              const u16 days_since_1970,
                                              const double seconds_since_midnight) {
  if(tow == tow_cache_) {
    days_since_1970_cache_ = days_since_1970;
    seconds_since_midnight_cache_ = seconds_since_midnight;
    utc_time_cached_ = true;
  }
}

void SbpToNmea2kConverter::SetPgn129029CacheFields(const msg_pos_llh_t *msg) {
  if(msg->tow == tow_cache_) {
    lat_cache_ = msg->lat;
    lon_cache_ = msg->lon;
    alt_cache_ = msg->height;
    sat_count_cache_ = msg->n_sats;  // Sats used in solution.
    gnss_metod_cache_ = gnss_method_lut_[msg->flags & 0x07];
    pos_llh_cached_ = true;
  }
}

void SbpToNmea2kConverter::SetPgn129029CacheFields(const msg_dops_t *msg) {
  if (msg->tow == tow_cache_) {
    hdop_cache_ = static_cast<double>(msg->hdop) / 100.0;
    pdop_cache_ = static_cast<double>(msg->pdop) / 100.0;
    dops_cached_ = true;
  }
}

void SbpToNmea2kConverter::ResetPgn129029Cache() {
  utc_time_cached_ = pos_llh_cached_ = dops_cached_ = false;
}

u8 SbpToNmea2kConverter::tow_to_sid(const u32 tow) {
  // 251 is prime and close to the SID limit of 252.
  return (last_sid = tow % 251);
}