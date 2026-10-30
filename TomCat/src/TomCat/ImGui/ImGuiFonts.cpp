#include "tcpch.h"
#include "ImGuiFonts.h"
#include <imgui.h>
#include <filesystem>

namespace TomCat {
	ImFont* BuildImGuiFonts(ImFontAtlas& fonts, float fontSize, bool editorStyling)
	{
		fonts.Clear();
		const char* regular = "Packages/fonts/opensans/OpenSans-Regular.ttf";
		const char* bold = "Packages/fonts/opensans/OpenSans-Bold.ttf";
#if defined(TC_PLATFORM_WINDOWS)
		if (editorStyling && std::filesystem::exists("C:/Windows/Fonts/segoeui.ttf")) regular = "C:/Windows/Fonts/segoeui.ttf";
		if (editorStyling && std::filesystem::exists("C:/Windows/Fonts/segoeuib.ttf")) bold = "C:/Windows/Fonts/segoeuib.ttf";
#endif
		fonts.AddFontFromFileTTF(bold, fontSize);
		ImFont* defaultFont = fonts.AddFontFromFileTTF(regular, fontSize);

		if (fonts.Fonts.size() >= 2)
		{
			static const char* kUIChineseChars = "\u4E00\u4E09\u4E0B\u4E0D\u4E0E\u4E14\u4E24\u4E2A\u4E2D\u4E32\u4E3A\u4E5F\u4E86\u4E8B\u4E8E\u4E92\u4EA4\u4ECE\u4EE3\u4EE5\u4EF6\u4EFB\u4F1A\u4F4D\u4F53\u4F55\u4F5C\u4F60\u4F7F\u4F9B\u4FA7\u4FBF\u4FDD\u4FEE\u503C\u504F\u50A8\u50CF\u5141\u5149\u514D\u5165\u5168\u5173\u5185\u518C\u51B2\u51FA\u51FB\u51FD\u5206\u5207\u5217\u521B\u521D\u5220\u5230\u5236\u5237\u524D\u529F\u52A0\u5305\u5316\u5339\u533A\u5355\u5373\u5386\u539F\u53BB\u53CC\u53D1\u53D6\u53D8\u53E0\u53E3\u53EA\u53EF\u53F3\u540C\u540D\u5411\u5426\u5458\u547D\u548C\u5668\u56DE\u56F4\u56FE\u5728\u573A\u5757\u5782\u57DF\u589E\u58F0\u5904\u590D\u5916\u591A\u5929\u5931\u5939\u5982\u59CB\u5B50\u5B57\u5B58\u5B89\u5B8C\u5B9E\u5BB9\u5BBD\u5BF8\u5BF9\u5BFC\u5C06\u5C0F\u5C1A\u5C1D\u5C3A\u5C40\u5C42\u5C45\u5C51\u5C55\u5DE6\u5DF2\u5E03\u5E73\u5E74\u5E76\u5E94\u5EA6\u5EFA\u5F00\u5F0F\u5F15\u5F39\u5F52\u5F53\u5F55\u5F84\u5FD7\u6001\u6027\u60AC\u60F3\u6210\u6216\u6240\u624D\u6253\u6267\u627E\u6298\u62D6\u62DF\u62E9\u62FD\u6309\u6362\u6377\u63A5\u63A7\u63CF\u63D0\u641C\u6444\u64AD\u64CD\u64CE\u6539\u653E\u6548\u6570\u6574\u6587\u65AD\u65B0\u65B9\u65E0\u65F6\u660E\u662F\u663E\u666E\u666F\u66F4\u6700\u6708\u6709\u671F\u672A\u672C\u673A\u675F\u6761\u677F\u6784\u679C\u67D3\u67E5\u6807\u680F\u6811\u6837\u6839\u6846\u68C0\u6A21\u6B21\u6B63\u6B8A\u6BCF\u6C38\u6CA1\u6CD5\u6CE8\u6D4F\u6D6E\u6D88\u6DFB\u6E05\u6E32\u6ED1\u70B9\u7126\u7247\u7248\u7269\u7279\u72B6\u73B0\u7406\u7528\u7531\u7559\u7565\u7684\u76D8\u76EE\u76F4\u76F8\u77E5\u7801\u786E\u78C1\u793A\u7981\u79F0\u79FB\u7A7A\u7A97\u7B26\u7B7E\u7B97\u7BA1\u7D22\u7D27\u7EA7\u7EC4\u7EC8\u7ED3\u7ED8\u7EDD\u7F13\u7F16\u7F29\u7F6E\u8005\u800C\u80CC\u80FD\u81F4\u822A\u8272\u8282\u8303\u83B7\u83DC\u865A\u884C\u8868\u88AB\u88C5\u8981\u89C6\u89C8\u89D2\u89E6\u8A00\u8BA1\u8BA4\u8BB0\u8BB8\u8BBE\u8BBF\u8BD5\u8BDD\u8BE5\u8BF7\u8BFB\u8C03\u8D25\u8D34\u8DDD\u8DDF\u8DEF\u8E2A\u8F7D\u8F91\u8F93\u8FB9\u8FD1\u8FD8\u8FD9\u8FDB\u8FDC\u8FF0\u9000\u9009\u900F\u9012\u901A\u903B\u904D\u907F\u90E8\u914D\u91CC\u91CD\u91CF\u949F\u94AE\u952E\u95ED\u95EE\u95F4\u9645\u9664\u9694\u9699\u96F6\u9700\u975E\u9762\u9876\u9879\u9884\u9898\u9AD8\u9ED8\u9F50";

			ImFontGlyphRangesBuilder builder;
			builder.AddRanges(fonts.GetGlyphRangesChineseSimplifiedCommon()); // common-char fallback
			static const ImWchar extraRanges[] = {
				0x2014, 0x2015,   // em dash
				0x2018, 0x201E,   // curly quotes
				0x2026, 0x2026,   // ellipsis
				0x3000, 0x303F,   // CJK punctuation
				0xFF00, 0xFFEF,   // full-width forms
				0
			};
			builder.AddRanges(extraRanges);
			builder.AddText(kUIChineseChars); // dynamically collect all UI Chinese chars

			static ImVector<ImWchar> sUIRanges; // must stay alive until the font atlas is built
			builder.BuildRanges(&sUIRanges);

			// Symbol ranges (U+22EE and friends) from Segoe UI Symbol
			static const ImWchar symRanges[] = {
				0x22EE, 0x22EE,   // vertical ellipsis
				0x2190, 0x2193,   // arrows (left/up/right/down)
				0x2196, 0x2199,   // diagonal arrows
				0x25B6, 0x25B6,   // black right-pointing triangle
				0x25C0, 0x25C0,   // black left-pointing triangle
				0x25B2, 0x25B2,   // black up-pointing triangle
				0x25BC, 0x25BC,   // black down-pointing triangle
				0x25CF, 0x25CF,   // black circle
				0x25A0, 0x25A0,   // black square
				0
			};

	#ifdef __EMSCRIPTEN__
			const char* cnFontPath = "WebFonts/NotoSansSC-Regular.otf";
			const char* symFontPath = cnFontPath;
	#elif defined(TC_PLATFORM_LINUX)
			const char* cnFontPath = "Packages/fonts/noto/NotoSansSC-Regular.otf";
			const char* symFontPath = cnFontPath;
	#else
			const char* cnFontPath = editorStyling && std::filesystem::exists("C:/Windows/Fonts/msyh.ttc") ? "C:/Windows/Fonts/msyh.ttc" : "C:/Windows/Fonts/simhei.ttf";
			const char* symFontPath = "C:/Windows/Fonts/seguisym.ttf";
	#endif

			ImFontConfig mergeCfg;
			mergeCfg.MergeMode = true;

			if (std::filesystem::exists(cnFontPath))
			{
				mergeCfg.DstFont = fonts.Fonts[0]; // OpenSans-Bold + Chinese
				fonts.AddFontFromFileTTF(cnFontPath, fontSize, &mergeCfg, sUIRanges.Data);
				mergeCfg.DstFont = fonts.Fonts[1]; // OpenSans-Regular + Chinese (default)
				fonts.AddFontFromFileTTF(cnFontPath, fontSize, &mergeCfg, sUIRanges.Data);
			}
			else
			{
				TC_Core_Warn("UI Chinese font is missing: {0}; rebuild the application to stage its Packages resources", cnFontPath);
			}
			if (std::filesystem::exists(symFontPath))
			{
				mergeCfg.DstFont = fonts.Fonts[0];
				fonts.AddFontFromFileTTF(symFontPath, fontSize, &mergeCfg, symRanges);
				mergeCfg.DstFont = fonts.Fonts[1];
				fonts.AddFontFromFileTTF(symFontPath, fontSize, &mergeCfg, symRanges);
			}
		}
		return defaultFont;
	}
}
