#pragma once

#include "../buffer/out/TextColor.h"
#include "DispatchTypes.hpp"

namespace Microsoft::Console::VirtualTerminal
{
    class SixelBuffer
    {
    public:
        SixelBuffer() noexcept;
        ~SixelBuffer() = default;

        bool SetPixelAspectRatio(const VTInt pixelAspectRatio) noexcept;
        bool SetBackgroundColorOptions(const VTInt backgroundColorOptions) noexcept;
        bool SetHorizontalGridSize(const VTInt horizontalGridSize) noexcept;
        bool SetPalette(std::span<const COLORREF> palette) noexcept;
        bool SetBackgroundColor(const COLORREF backgroundColor) noexcept;

        std::span<const COLORREF> GetPixels() const noexcept;
        til::size GetSize() const noexcept;

        void AddData(const wchar_t ch);
        bool FinalizeData();

    private:
        static constexpr size_t COLOR_TABLE_SIZE = UINT16_MAX + 1;
        static constexpr uint16_t COLOR_TABLE_BACKGROUND = COLOR_TABLE_SIZE - 1;
        static constexpr uint16_t COLOR_TABLE_TRANSPARENT = COLOR_TABLE_SIZE - 2;
        static constexpr size_t PALETTE_SIZE = COLOR_TABLE_SIZE - 2;

        std::vector<COLORREF> _pixels;
        til::size _pixelBufferSize;
        til::size _pixelSize;
        til::size _attributedSize;

        std::array<COLORREF, COLOR_TABLE_SIZE> _colorTable;
        static const std::array<COLORREF, 16> s_defaultColorTable;
        void _setPaletteColor(const COLORREF color, const size_t index) noexcept;

        static constexpr size_t SIXEL_SIZE = 6;
        typedef std::array<uint16_t, SIXEL_SIZE> Sixel;
        typedef std::vector<Sixel> SixelLine;
        typedef std::vector<SixelLine> SixelImage;
        SixelImage _buffer;

        template<typename T, std::size_t... indexes>
        constexpr auto makeSixel(T&& value, std::index_sequence<indexes...>)
        {
            return std::array<std::decay_t<T>, sizeof...(indexes)>{ (static_cast<void>(indexes), value)... };
        }

        template<typename T>
        constexpr auto makeSixel(T&& value)
        {
            return makeSixel(std::forward<T>(value), std::make_index_sequence<SIXEL_SIZE>{});
        }

        void _addSixelValue(const VTInt value);

        bool _paletteZeroIsBackgroundColor{ false };

        enum class ParseState
        {
            Uninitialized = 0,
            Ground,
            SetRasterAttributes,        // DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv
            GraphicsRepeatIntroducer,   // DECGRI Graphics Repeat Introducer ! Pn Ch
            GraphicsColorIntroducer,    // DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz
            Finished,
        };

        ParseState _parseState{ ParseState::Uninitialized };
        size_t _sixelRow{ 0 };
        size_t _sixelColumn{ 0 };
        SixelLine& _sixelLine();
        Sixel& _sixelValue();
        VTInt _repeatCount{ 0 };
        uint16_t _currentPalette{ 0 };
        bool _hasReceivedSixelData{ false };

        // StateMachine
        std::list<VTParameter> _parameters;
        bool _parameterLimitOverflowed{ false };
        void _accumulateTo(const wchar_t ch, VTInt& value) noexcept;
        void _newParamStack();
        void _addParam(const wchar_t ch);
        VTParameter _popParamBack();
        VTParameter _popParamFront();

        void _initializeParser();
        void _parseGround(const wchar_t ch);
        void _parseRasterAttributes(const wchar_t ch);
        void _parseGraphicsRepeat(const wchar_t ch);
        void _parseGraphicsColor(const wchar_t ch);
    };
}
