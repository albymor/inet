//
// Copyright (C) 2014 OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#include "Ieee80211OFDMDemodulator.h"
#include "QAM16Modulation.h"
#include "QAM64Modulation.h"
#include "BPSKModulation.h"
#include "QPSKModulation.h"
#include "APSKSymbol.h"
#include "SignalBitModel.h"

namespace inet {
namespace physicallayer {

void Ieee80211OFDMDemodulator::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL)
        signalModulationScheme = &BPSKModulation::singleton;
}

BitVector Ieee80211OFDMDemodulator::demodulateSignalSymbol(const OFDMSymbol *signalSymbol) const
{
    return demodulateField(signalSymbol, signalModulationScheme);
}

BitVector Ieee80211OFDMDemodulator::demodulateDataSymbol(const OFDMSymbol *dataSymbol) const
{
    return demodulateField(dataSymbol, dataModulationScheme);
}

BitVector Ieee80211OFDMDemodulator::demodulateField(const OFDMSymbol *signalSymbol, const APSKModulationBase* modulationScheme) const
{
    std::vector<const APSKSymbol*> apskSymbols = signalSymbol->getSubCarrierSymbols();
    BitVector field;
    for (unsigned int i = 0; i < apskSymbols.size(); i++)
    {
        const APSKSymbol *apskSymbol = apskSymbols.at(i);
        ShortBitVector bits = signalModulationScheme->demapToBitRepresentation(apskSymbol);
        for (unsigned int j = 0; i < bits.getSize(); j++)
            field.appendBit(bits.getBit(j));
    }
    return field;
}

const IReceptionBitModel* Ieee80211OFDMDemodulator::createBitModel(const BitVector *bitRepresentation) const
{
    ShortBitVector rate = getRate(bitRepresentation);
    // The bits R1–R4 shall be set, dependent on RATE, according to the values in Table 18-6.
    const Ieee80211ConvolutionalCode *fecInfo = getFecFromSignalFieldRate(rate);
    ShortBitVector length;
    // The PLCP LENGTH field shall be an unsigned 12-bit integer that indicates the number of octets in the
    // PSDU that the MAC is currently requesting the PHY to transmit. This value is used by the PHY to
    // determine the number of octet transfers that will occur between the MAC and the PHY after receiving a
    // request to start transmission.
    for (int i = 4; i < 16; i++)
        length.appendBit(bitRepresentation->getBit(i));
    // Note:
    // IScramblerInfo is NULL, since their info is IEEE 802.11 specific (that is,the scrambling
    // method always uses the S(x) = x^7 + x^4 + 1 polynomial.
    // The permutation equations of the interleaving method is fixed but some parameters may vary,
    // those parameters depend on the modulation scheme which we can compute from the SIGNAL field.
    // TODO: ber, bitErrorCount, bitRate
    // TODO: ConvoluationalCoderInfo needs to be factored out from ConvoluationalCoder
    // TODO: Other infos also need to be factored out from their parent classes.
    const Ieee80211Interleaving *interleaving = getInterleavingFromModulation();
    return new ReceptionBitModel(bitRepresentation->getSize(), 0, bitRepresentation, fecInfo, NULL, interleaving, 0, 0);
}

const Ieee80211ConvolutionalCode* Ieee80211OFDMDemodulator::getFecFromSignalFieldRate(const ShortBitVector& rate) const
{
    // Table 18-6—Contents of the SIGNAL field
    // Table 18-4—Modulation-dependent parameters
    if (rate == ShortBitVector("1101") || rate == ShortBitVector("0101") || rate == ShortBitVector("1001"))
        return new Ieee80211ConvolutionalCode(1, 2);
    else if (rate == ShortBitVector("1111") || rate == ShortBitVector("0111") || rate == ShortBitVector("1011") ||
             rate == ShortBitVector("0111"))
        return new Ieee80211ConvolutionalCode(3, 4);
    else if (rate == ShortBitVector("0001"))
        return new Ieee80211ConvolutionalCode(2, 3);
    else
        throw cRuntimeError("Unknown rate field  = %s", rate.toString().c_str());
}

const APSKModulationBase* Ieee80211OFDMDemodulator::getModulationFromSignalFieldRate(const ShortBitVector& rate) const
{
    // Table 18-6—Contents of the SIGNAL field
    // Table 18-4—Modulation-dependent parameters
    if (rate == ShortBitVector("1101") || rate == ShortBitVector("1111"))
        return &BPSKModulation::singleton;
    else if (rate == ShortBitVector("0101") || rate == ShortBitVector("0111"))
        return &QPSKModulation::singleton;
    else if (rate == ShortBitVector("1001") || rate == ShortBitVector("1011"))
        return &QAM16Modulation::singleton;
    else if(rate == ShortBitVector("0001") || rate == ShortBitVector("0011"))
        return &QAM64Modulation::singleton;
    else
        throw cRuntimeError("Unknown rate field = %s", rate.toString().c_str());
}

ShortBitVector Ieee80211OFDMDemodulator::getRate(const BitVector* signalField) const
{
    ShortBitVector rate;
    for (int i = 0; i < 4; i++)
        rate.appendBit(signalField->getBit(i));
    return rate;
}

void Ieee80211OFDMDemodulator::setDataFieldDemodulation(const BitVector *signalField) const
{
    ShortBitVector rate = getRate(signalField);
    dataModulationScheme = getModulationFromSignalFieldRate(rate);
}

const Ieee80211Interleaving* Ieee80211OFDMDemodulator::getInterleavingFromModulation() const
{
    return new Ieee80211Interleaving(dataModulationScheme->getCodeWordLength() * 48, dataModulationScheme->getCodeWordLength());
}

const IReceptionBitModel* Ieee80211OFDMDemodulator::demodulate(const IReceptionSymbolModel* symbolModel) const
{
    const std::vector<const ISymbol*> *symbols = symbolModel->getSymbols();
    const OFDMSymbol *signalSymbol = dynamic_cast<const OFDMSymbol *>(symbols->at(0)); // The first OFDM symbol is the SIGNAL symbol
    BitVector *bitRepresentation = new BitVector(demodulateSignalSymbol(signalSymbol));
    setDataFieldDemodulation(bitRepresentation);
    for (unsigned int i = 1; i < symbols->size(); i++)
    {
        const OFDMSymbol *dataSymbol = dynamic_cast<const OFDMSymbol *>(symbols->at(i));
        BitVector dataBits = demodulateDataSymbol(dataSymbol);
        for (unsigned int j = 0; j < dataBits.getSize(); j++)
            bitRepresentation->appendBit(dataBits.getBit(j));
    }
    return createBitModel(bitRepresentation);
}

} // namespace physicallayer
} // namespace inet
