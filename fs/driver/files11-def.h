// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "volume.h"

namespace U::FS::Files11 {

enum Attr {
	FH2$M_DIRECTORY = 1 << 13
	, FH2$M_MARKDEL = 1 << 15
	, FH2$M_ERASE = 1 << 17
};


struct HomeBlock {
	uint32_t HomeLBN;
	uint32_t AlHomeLBN;
	uint32_t AltIdxLBN;
	uint16_t StrucLev;
};


} // U::FS::Files11
