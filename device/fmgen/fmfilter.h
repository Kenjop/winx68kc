// ---------------------------------------------------------------------------
//	LPF module for FM sound generator
//	Copyright (C) cisc 2001.
// ---------------------------------------------------------------------------
//	$Id$

#ifndef FMFILTER_H
#define FMFILTER_H

#include "types.h"



namespace FM
{

// ---------------------------------------------------------------------------
//	ƒtƒBƒ‹ƒ^
//
class LPF
{
	enum
	{
		order	= 2,
		nchs	= 2,
		F		= 4096,
	};

public:
	void MakeFilter(uint cutoff, uint pcmrate);
	int Filter(uint ch, int o);

private:
	int fn[order][4];
	int b[nchs][order][2];
};

// ---------------------------------------------------------------------------

inline int LPF::Filter(uint ch, int o)
{
	for (int j=0; j<order; j++)
	{
		int p = o + (b[ch][j][0] * fn[j][0] + b[ch][j][1] * fn[j][1]) / F;
		o = (p * fn[j][2] + b[ch][j][0] * fn[j][3] + b[ch][j][1] * fn[j][2]) / F;
		b[ch][j][1] = b[ch][j][0];
		b[ch][j][0] = p;
	}
	return o;
}

}

#endif // FMFILTER_H
