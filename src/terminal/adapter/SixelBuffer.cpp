#include "precomp.h"
#include "SixelBuffer.hpp"
#include "../../types/inc/utils.hpp"
#include "../../terminal/parser/stateMachine.hpp"

using namespace Microsoft::Console::VirtualTerminal;

decltype(SixelBuffer::s_defaultColorTable) SixelBuffer::s_defaultColorTable = {
    Utils::ColorFromRGB100(0, 0, 0),    /*  0 Black    */
    Utils::ColorFromRGB100(20, 20, 80), /*  1 Blue     */
    Utils::ColorFromRGB100(80, 13, 13), /*  2 Red      */
    Utils::ColorFromRGB100(20, 80, 20), /*  3 Green    */
    Utils::ColorFromRGB100(80, 20, 80), /*  4 Magenta  */
    Utils::ColorFromRGB100(20, 80, 80), /*  5 Cyan     */
    Utils::ColorFromRGB100(80, 80, 20), /*  6 Yellow   */
    Utils::ColorFromRGB100(53, 53, 53), /*  7 Gray 50% */
    Utils::ColorFromRGB100(26, 26, 26), /*  8 Gray 25% */
    Utils::ColorFromRGB100(33, 33, 60), /*  9 Blue*    */
    Utils::ColorFromRGB100(60, 26, 26), /* 10 Red*     */
    Utils::ColorFromRGB100(33, 60, 33), /* 11 Green*   */
    Utils::ColorFromRGB100(60, 33, 60), /* 12 Magenta* */
    Utils::ColorFromRGB100(33, 60, 60), /* 13 Cyan*    */
    Utils::ColorFromRGB100(60, 60, 33), /* 14 Yellow*  */
    Utils::ColorFromRGB100(80, 80, 80), /* 15 Gray 75% */
};

SixelBuffer::SixelBuffer() noexcept
{
    SetPixelAspectRatio(0);
    SetBackgroundColorOptions(0);
    SetHorizontalGridSize(0);

    _colorTable[COLOR_TABLE_TRANSPARENT] = /* 0 ALPHA */ 0x00ff00ff;
};

bool SixelBuffer::SetPixelAspectRatio(const VTInt pixelAspectRatio) noexcept
{
    switch (pixelAspectRatio)
    {
    case 0:
    case 1:
    case 5:
    case 6:
        _pixelSize.height = 2;
        return true;
    case 2:
        _pixelSize.height = 5;
        return true;
    case 3:
    case 4:
        _pixelSize.height = 3;
        return true;
    case 7:
    case 8:
    case 9:
        _pixelSize.height = 1;
        return true;
    default:
        return false;
    }
}

bool SixelBuffer::SetBackgroundColorOptions(const VTInt backgroundColorOptions) noexcept
{
    switch (backgroundColorOptions)
    {
    case 0:
    case 2:
        _paletteZeroIsBackgroundColor = true;
        return true;
    case 1:
        _paletteZeroIsBackgroundColor = false;
        return true;
    default:
        return false;
    }
}

bool SixelBuffer::SetHorizontalGridSize(const VTInt horizontalGridSize) noexcept
{
    if (horizontalGridSize > 0)
    {
        _pixelSize.width = horizontalGridSize;
    }
    else
    {
        _pixelSize.width = 1;
    }

    return true;
}

bool SixelBuffer::SetPalette(std::span<const COLORREF> palette) noexcept
{
    auto i = 0u;
    
    for (; i < PALETTE_SIZE && i < palette.size(); i++)
    {
        _colorTable[i] = 0xff000000 | palette[i];
    }

    for (; i < s_defaultColorTable.size(); i++)
    {
        _colorTable[i] = 0xff000000 | s_defaultColorTable[i];
    }

    for (; i < PALETTE_SIZE; i++)
    {
        _colorTable[i] = 0xff000000 | RGB(0, 0, 0);
    }

    return true;
}

bool SixelBuffer::SetBackgroundColor(const COLORREF backgroundColor) noexcept
{
    _colorTable[COLOR_TABLE_BACKGROUND] = 0xff000000 | backgroundColor;

    return true;
}

std::span<const COLORREF> SixelBuffer::GetPixels() const noexcept
{
    return _pixels;
}

til::size SixelBuffer::GetSize() const noexcept
{
    return _pixelBufferSize;
}

static constexpr bool _isNumericParamValue(const wchar_t ch) noexcept
{
    return ch >= L'0' && ch <= L'9'; // 0x30 - 0x39
}

static constexpr bool _isParameterDelimiter(const wchar_t ch) noexcept
{
    return ch == L';'; // 0x3B
}

static constexpr wchar_t s_sixelMinValue = L'?'; // 0x3F
static constexpr wchar_t s_sixelMaxValue = L'~'; // 0x7E

static constexpr bool _isSixelValue(const wchar_t ch) noexcept
{
    return ch >= s_sixelMinValue && ch <= s_sixelMaxValue;
}

void SixelBuffer::AddData(const wchar_t ch)
{
    switch (_parseState)
    {
    case ParseState::Finished:
    case ParseState::Uninitialized:
        _initializeParser();
        __fallthrough;
    case ParseState::Ground:
        _parseGround(ch);
        break;
    case ParseState::SetRasterAttributes:
        _parseRasterAttributes(ch);
        break;
    case ParseState::GraphicsRepeatIntroducer:
        _parseGraphicsRepeat(ch);
        break;
    case ParseState::GraphicsColorIntroducer:
        _parseGraphicsColor(ch);
        break;
        DEFAULT_UNREACHABLE;
    }
}

bool SixelBuffer::FinalizeData()
{
    switch (_parseState)
    {
    case ParseState::Uninitialized:
        return false;
    case ParseState::Ground:
    case ParseState::SetRasterAttributes:
    case ParseState::GraphicsRepeatIntroducer:
    case ParseState::GraphicsColorIntroducer:
        break;
    case ParseState::Finished:
        return true;
        DEFAULT_UNREACHABLE;
    }

    size_t maxLineSize = 0;

    for (auto line = _buffer.rbegin(); line != _buffer.rend(); line++)
    {
        for (auto sixel = line->rbegin(); sixel != line->rend(); sixel++)
        {
            if (static_cast<size_t>(std::count(sixel->begin(), sixel->end(), COLOR_TABLE_TRANSPARENT)) == sixel->size())
            {
                line->pop_back();
            }
        }

        if (line->empty())
        {
            _buffer.pop_back();
        }
        else
        {
            maxLineSize = std::max(maxLineSize, line->size());
        }
    }

    _pixelBufferSize = { gsl::narrow<til::CoordType>(maxLineSize * _pixelSize.width), gsl::narrow<til::CoordType>(_buffer.size() * _pixelSize.height * SIXEL_SIZE) };
    _pixels = std::vector<COLORREF>(_pixelBufferSize.width * _pixelBufferSize.height);

    size_t lastOpaquePixel = 0;

    if (_paletteZeroIsBackgroundColor)
    {
        _colorTable[0] = _colorTable[COLOR_TABLE_BACKGROUND];
    }

    _sixelRow = 0;

    for (auto line = _buffer.begin(); line != _buffer.end(); line++, _sixelRow++)
    {
        _sixelColumn = 0;

        for (auto sixel = line->begin(); sixel != line->end(); sixel++, _sixelColumn++)
        {
            for (auto pixel = 0u; pixel < sixel->size(); pixel++)
            {
                auto cti = sixel->at(pixel);
                auto col = _colorTable[cti];

                for (auto pj = 0; pj < _pixelSize.height; pj++)
                {
                    for (auto pi = 0; pi < _pixelSize.width; pi++)
                    {
                        auto px = _sixelColumn * _pixelSize.width + pi;
                        auto py = _sixelRow * SIXEL_SIZE * _pixelSize.height + pixel * _pixelSize.height + pj;
                        auto pindex = py * _pixelBufferSize.width + px;
                        _pixels.at(pindex) = col;

                        if (line == _buffer.end() - 1 && pj == _pixelSize.height - 1 && cti != COLOR_TABLE_TRANSPARENT)
                        {
                            lastOpaquePixel = pindex;
                        }
                    }
                }
            }
        }
    }

    auto rowsToErase = ((_pixels.size() - 1) - lastOpaquePixel) / _pixelBufferSize.width;
    if (rowsToErase > 0)
    {
        _pixelBufferSize.height -= gsl::narrow_cast<til::CoordType>(rowsToErase);
        _pixels.resize(_pixelBufferSize.width * _pixelBufferSize.height);
    }

    _sixelRow = 0;
    _sixelColumn = 0;
    _buffer.clear();

    _parseState = ParseState::Finished;

    return !_pixels.empty() && _pixelBufferSize != til::size{};
}

void SixelBuffer::_setPaletteColor(const COLORREF color, const size_t index) noexcept
{
    if (index < PALETTE_SIZE)
    {
        _colorTable[index] = 0xff000000 | color;
    }
}

void SixelBuffer::_addSixelValue(const VTInt value)
{
    if (value == 0)
    {
        _sixelColumn += _repeatCount;
    }
    else if (value == (s_sixelMaxValue - s_sixelMinValue))
    {
        for (VTInt r = 0; r < _repeatCount; r++, _sixelColumn++)
        {
            _sixelValue().fill(_currentPalette);
        }
    }
    else
    {
        VTInt sixelVerticalMask = 0x01;
        if (_repeatCount == 1)
        {
            for (auto i = 0u; i < SIXEL_SIZE; i++)
            {
                if ((value & sixelVerticalMask) != 0)
                {
                    _sixelValue().at(i) = _currentPalette;
                }
                sixelVerticalMask <<= 1;
            }
            _sixelColumn++;
        }
        else if (_repeatCount > 1)
        {
            for (auto i = 0u; i < SIXEL_SIZE; i++)
            {
                if ((value & sixelVerticalMask) != 0)
                {
                    VTInt cc = sixelVerticalMask << 1;
                    auto n = 1u;
                    for (; (i + n) < SIXEL_SIZE; n++)
                    {
                        if ((value & cc) == 0)
                            break;

                        cc <<= 1;
                    }

                    for (VTInt r = 0; r < _repeatCount; r++, _sixelColumn++)
                    {
                        std::fill_n(_sixelValue().begin() + i, n, _currentPalette);
                    }
                    _sixelColumn -= _repeatCount;

                    i += (n - 1);
                    sixelVerticalMask <<= (n - 1);
                }
                sixelVerticalMask <<= 1;
            }
            _sixelColumn += _repeatCount;
        }
    }
    _repeatCount = 1;
}

SixelBuffer::SixelLine& SixelBuffer::_sixelLine()
{
    if (_sixelRow >= _buffer.size())
    {
        size_t diff = _sixelRow + 1 - _buffer.size();

        if (diff > 1)
        {
            _buffer.reserve(_buffer.size() + diff);
        }

        _buffer.insert(_buffer.end(), diff, {});
    }

    return til::at(_buffer, _sixelRow);
}

SixelBuffer::Sixel& SixelBuffer::_sixelValue()
{
    auto& line = _sixelLine();

    if (_sixelColumn >= line.size())
    {
        size_t diff = _sixelColumn + 1 - line.size();

        if (diff > 1)
        {
            line.reserve(line.size() + diff);
        }

        line.insert(line.end(), diff, makeSixel(COLOR_TABLE_TRANSPARENT));
    }

    return til::at(line, _sixelColumn);
}

void SixelBuffer::_accumulateTo(const wchar_t ch, VTInt& value) noexcept
{
    const auto digit = ch - L'0';

    value = value * 10 + digit;

    // Values larger than the maximum should be mapped to the largest supported value.
    if (value > MAX_PARAMETER_VALUE)
    {
        value = MAX_PARAMETER_VALUE;
    }
}

void SixelBuffer::_newParamStack()
{
    _parameters.clear();
    _parameters.push_back({});
    _parameterLimitOverflowed = false;
}

void SixelBuffer::_addParam(const wchar_t ch)
{
    // Once we've reached the parameter limit, additional parameters are ignored.
    if (!_parameterLimitOverflowed)
    {
        // If we have no parameters and we're about to add one, get the next value ready here.
        if (_parameters.empty())
        {
            _parameters.push_back({});
        }

        // On a delimiter, increase the number of params we've seen.
        // "Empty" params should still count as a param -
        //      eg "\x1b[0;;m" should be three params
        if (_isParameterDelimiter(ch))
        {
            // If we receive a delimiter after we've already accumulated the
            // maximum allowed parameters, then we need to set a flag to
            // indicate that further parameter characters should be ignored.
            if (_parameters.size() >= MAX_PARAMETER_COUNT)
            {
                _parameterLimitOverflowed = true;
            }
            else
            {
                // Otherwise move to next param.
                _parameters.push_back({});
            }
        }
        else
        {
            // Accumulate the character given into the last (current) parameter.
            // If the value hasn't been initialized yet, it'll start as 0.
            auto currentParameter = _parameters.back().value_or(0);
            _accumulateTo(ch, currentParameter);
            _parameters.back() = currentParameter;
        }
    }
}

VTParameter SixelBuffer::_popParamBack()
{
    assert(!_parameters.empty());

    VTParameter temp = _parameters.back();
    _parameters.pop_back();

    return temp;
}

VTParameter SixelBuffer::_popParamFront()
{
    assert(!_parameters.empty());

    VTParameter temp = _parameters.front();
    _parameters.pop_front();

    return temp;
}

void SixelBuffer::_initializeParser()
{
    assert(_parseState == ParseState::Uninitialized
        || _parseState == ParseState::Finished);

    _buffer.clear();
    _attributedSize = { 0, 0 };

    _sixelRow = 0;
    _sixelColumn = 0;

    _repeatCount = 1;
    _currentPalette = 15; // a foreground color
    _hasReceivedSixelData = false;

    _newParamStack();

    _parseState = ParseState::Ground;
}

void SixelBuffer::_parseGround(const wchar_t ch)
{
    assert(_parseState == ParseState::Ground);

    switch (ch)
    {
    case L'"': // DECGRA
        _parseState = ParseState::SetRasterAttributes;
        _newParamStack();
        break;
    case L'!': // DECGRI
        _parseState = ParseState::GraphicsRepeatIntroducer;
        _newParamStack();
        break;
    case L'#': // DECGCI
        _parseState = ParseState::GraphicsColorIntroducer;
        _newParamStack();
        break;
    case L'-': // DECGNL Graphics Next Line
        _sixelRow++;
        __fallthrough;
    case L'$': // DECGCR Graphics Carriage Return
        _sixelColumn = 0;
        break;
    default:
        if (_isSixelValue(ch))
        {
            _addSixelValue(ch - s_sixelMinValue);
        }
        break;
    }

    if (_parseState != ParseState::SetRasterAttributes)
    {
        _hasReceivedSixelData = true;
    }
}

void SixelBuffer::_parseRasterAttributes(const wchar_t ch)
{
    assert(_parseState == ParseState::SetRasterAttributes);

    if (_isNumericParamValue(ch) || _isParameterDelimiter(ch))
    {
        _addParam(ch);
    }
    else
    {
        if (!_hasReceivedSixelData)
        {
            if (!_parameters.empty())
            {
                _pixelSize.height = _popParamFront();
            }

            if (!_parameters.empty())
            {
                _pixelSize.width = _popParamFront();
            }

            if (!_parameters.empty())
            {
                VTInt ph = _popParamFront().value_or(0);
                if (ph > 0)
                {
                    _attributedSize.width = ph;
                }
            }

            if (!_parameters.empty())
            {
                VTInt pv = _popParamFront().value_or(0);
                if (pv > 0)
                {
                    _attributedSize.height = pv;
                }
            }
        }

        _parseState = ParseState::Ground;
        _parseGround(ch);
    }
}

void SixelBuffer::_parseGraphicsRepeat(const wchar_t ch)
{
    assert(_parseState == ParseState::GraphicsRepeatIntroducer);

    if (_isNumericParamValue(ch))
    {
        _addParam(ch);
    }
    else
    {
        _repeatCount = _popParamFront(); // value_or(1) ?

        _parseState = ParseState::Ground;
        _parseGround(ch);
    }
}

void SixelBuffer::_parseGraphicsColor(const wchar_t ch)
{
    assert(_parseState == ParseState::GraphicsColorIntroducer);

    if (_isNumericParamValue(ch) || _isParameterDelimiter(ch))
    {
        _addParam(ch);
    }
    else
    {
        if (!_parameters.empty())
        {
            // Deliberate PALETTE_SIZE without -1 here. Out of range values palettes will become transparent.
            _currentPalette = gsl::narrow_cast<uint16_t>(std::clamp(_popParamFront().value_or(0), 0, static_cast<int>(PALETTE_SIZE))); // param % PALETTE_SIZE ?

            if (_parameters.size() > 3)
            {
                DispatchTypes::ColorModel colorModel = _popParamFront();
                const auto x = _popParamFront().value_or(0);
                const auto y = _popParamFront().value_or(0);
                const auto z = _popParamFront().value_or(0);

                switch (colorModel)
                {
                case DispatchTypes::ColorModel::HLS:
                    _setPaletteColor(Utils::ColorFromHLS(x, y, z), _currentPalette);
                    break;
                case DispatchTypes::ColorModel::RGB:
                    _setPaletteColor(Utils::ColorFromRGB100(x, y, z), _currentPalette);
                    break;
                }
            }
        }

        _parseState = ParseState::Ground;
        _parseGround(ch);
    }
}
