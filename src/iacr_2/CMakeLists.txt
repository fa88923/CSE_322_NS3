## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

# IACR (Interference Aware Cooperative Routing) Module
# Extends AODV with interference-aware metrics

build_lib(
  LIBNAME iacr_2
  SOURCE_FILES
    model/iacr-packet.cc
    model/iacr-routing-protocol.cc
    helper/iacr-helper.cc
  HEADER_FILES
    model/iacr-packet.h
    model/iacr-routing-protocol.h
    helper/iacr-helper.h
  LIBRARIES_TO_LINK
    ${libaodv}
)
