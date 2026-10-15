// Copyright (C) 2026 Kane. All Rights Reserved.

#include "TextFileSyntaxHighlighter.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/IRun.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextLayout.h"

namespace
{
	/** 让制表符按固定空格数渲染（默认字体度量下 \t 宽度不稳定） */
	class FTabAwareTextRun : public FSlateTextRun
	{
	public:
		static TSharedRef<FTabAwareTextRun> Create(const FRunInfo& InRunInfo, const TSharedRef<const FString>& InText, const FTextBlockStyle& InStyle, const FTextRange& InRange, int32 InTabSize)
		{
			return MakeShareable(new FTabAwareTextRun(InRunInfo, InText, InStyle, InRange, InTabSize));
		}

		virtual FVector2D Measure(int32 StartIndex, int32 EndIndex, float Scale, const FRunTextContext& TextContext) const override
		{
			const FVector2D ShadowOffsetToApply((EndIndex == Range.EndIndex) ? FMath::Abs(Style.ShadowOffset.X * Scale) : 0.0f, FMath::Abs(Style.ShadowOffset.Y * Scale));
			if (EndIndex - StartIndex == 0)
			{
				return FVector2D(ShadowOffsetToApply.X * Scale, GetMaxHeight(Scale));
			}

			int32 TabCount = 0;
			for (int32 Index = StartIndex; Index < EndIndex; ++Index)
			{
				if ((*Text)[Index] == TEXT('\t'))
				{
					++TabCount;
				}
			}

			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			FVector2D Size = FontMeasure->Measure(**Text, StartIndex, EndIndex, Style.Font, true, Scale) + ShadowOffsetToApply;
			Size.X -= TabWidth * TabCount * Scale;
			Size.X += SpaceWidth * TabCount * TabSize * Scale;
			return Size;
		}

	private:
		FTabAwareTextRun(const FRunInfo& InRunInfo, const TSharedRef<const FString>& InText, const FTextBlockStyle& InStyle, const FTextRange& InRange, int32 InTabSize)
			: FSlateTextRun(InRunInfo, InText, InStyle, InRange)
			, TabSize(InTabSize)
		{
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			TabWidth = FontMeasure->Measure(TEXT("\t"), 0, 1, Style.Font, true, 1.0f).X;
			SpaceWidth = FontMeasure->Measure(TEXT(" "), 0, 1, Style.Font, true, 1.0f).X;
		}

		int32 TabSize;
		double TabWidth = 0.0;
		double SpaceWidth = 0.0;
	};

	bool MatchAt(const FString& Line, int32 Index, const FString& Pattern)
	{
		return Pattern.Len() > 0 && Index + Pattern.Len() <= Line.Len() && FCString::Strncmp(&Line[Index], *Pattern, Pattern.Len()) == 0;
	}
}

TSharedRef<FTextFileSyntaxHighlighter> FTextFileSyntaxHighlighter::Create(const FTextFileLanguage* InLanguage, const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont, int32 InTabSize)
{
	return MakeShareable(new FTextFileSyntaxHighlighter(InLanguage, InColors, InFont, InTabSize));
}

FTextFileSyntaxHighlighter::FTextFileSyntaxHighlighter(const FTextFileLanguage* InLanguage, const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont, int32 InTabSize)
	: TabSize(FMath::Max(1, InTabSize))
{
	if (InLanguage)
	{
		Language = *InLanguage;
		for (const FString& Word : InLanguage->Keywords)
		{
			KeywordSet.Add(InLanguage->bCaseSensitive ? Word : Word.ToLower());
		}
		for (const FString& Word : InLanguage->Types)
		{
			TypeSet.Add(InLanguage->bCaseSensitive ? Word : Word.ToLower());
		}
	}
	RebuildStyles(InColors, InFont);
}

void FTextFileSyntaxHighlighter::UpdateStyle(const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont, int32 InTabSize)
{
	TabSize = FMath::Max(1, InTabSize);
	RebuildStyles(InColors, InFont);
	MakeDirty();
}

void FTextFileSyntaxHighlighter::SetHighlightEnabled(bool bInEnabled)
{
	if (bHighlightEnabled != bInEnabled)
	{
		bHighlightEnabled = bInEnabled;
		MakeDirty();
	}
}

void FTextFileSyntaxHighlighter::RebuildStyles(const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont)
{
	auto Make = [&InFont](const FLinearColor& Color)
	{
		return FTextBlockStyle().SetFont(InFont).SetColorAndOpacity(FSlateColor(Color)).SetShadowOffset(FVector2D::ZeroVector);
	};

	Styles[(int32)ETokenKind::Normal] = Make(InColors.Normal);
	Styles[(int32)ETokenKind::Keyword] = Make(InColors.Keyword);
	Styles[(int32)ETokenKind::Type] = Make(InColors.Type);
	Styles[(int32)ETokenKind::String] = Make(InColors.String);
	Styles[(int32)ETokenKind::Number] = Make(InColors.Number);
	Styles[(int32)ETokenKind::Comment] = Make(InColors.Comment);
	Styles[(int32)ETokenKind::Preprocessor] = Make(InColors.Preprocessor);
	Styles[(int32)ETokenKind::Operator] = Make(InColors.Operator);
	Styles[(int32)ETokenKind::Whitespace] = Make(InColors.Normal);
}

const FTextBlockStyle& FTextFileSyntaxHighlighter::GetStyle(ETokenKind Kind) const
{
	return Styles[(int32)Kind];
}

void FTextFileSyntaxHighlighter::TokenizeLine(const FString& Line, bool& bInOutBlockComment, TArray<FToken>& OutTokens) const
{
	OutTokens.Reset();
	const int32 Len = Line.Len();
	const FTextFileLanguage* InLanguage = bHighlightEnabled ? Language.GetPtrOrNull() : nullptr;

	auto Emit = [&OutTokens](ETokenKind Kind, int32 Begin, int32 End)
	{
		if (End > Begin)
		{
			OutTokens.Add({ Kind, FTextRange(Begin, End) });
		}
	};

	if (!InLanguage)
	{
		// 无语言规则：只区分空白与普通文本，便于 Tab 宽度处理
		int32 i = 0;
		while (i < Len)
		{
			const int32 Start = i;
			const bool bWhite = FChar::IsWhitespace(Line[i]);
			while (i < Len && FChar::IsWhitespace(Line[i]) == bWhite) { ++i; }
			Emit(bWhite ? ETokenKind::Whitespace : ETokenKind::Normal, Start, i);
		}
		return;
	}

	const FTextFileLanguage& Lang = *InLanguage;
	const bool bHasBlockComment = !Lang.BlockCommentStart.IsEmpty() && !Lang.BlockCommentEnd.IsEmpty();
	bool bSeenNonWhitespace = false;

	int32 i = 0;
	while (i < Len)
	{
		if (bInOutBlockComment)
		{
			const int32 EndIdx = Line.Find(Lang.BlockCommentEnd, ESearchCase::CaseSensitive, ESearchDir::FromStart, i);
			if (EndIdx == INDEX_NONE)
			{
				Emit(ETokenKind::Comment, i, Len);
				return;
			}
			const int32 After = EndIdx + Lang.BlockCommentEnd.Len();
			Emit(ETokenKind::Comment, i, After);
			i = After;
			bInOutBlockComment = false;
			continue;
		}

		const TCHAR C = Line[i];

		// 空白
		if (FChar::IsWhitespace(C))
		{
			const int32 Start = i;
			while (i < Len && FChar::IsWhitespace(Line[i])) { ++i; }
			Emit(ETokenKind::Whitespace, Start, i);
			continue;
		}

		// 块注释开始（需先于单行注释判断，如 Lua 的 --[[ 与 --）
		if (bHasBlockComment && MatchAt(Line, i, Lang.BlockCommentStart))
		{
			const int32 SearchFrom = i + Lang.BlockCommentStart.Len();
			const int32 EndIdx = Line.Find(Lang.BlockCommentEnd, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (EndIdx == INDEX_NONE)
			{
				Emit(ETokenKind::Comment, i, Len);
				bInOutBlockComment = true;
				return;
			}
			const int32 After = EndIdx + Lang.BlockCommentEnd.Len();
			Emit(ETokenKind::Comment, i, After);
			i = After;
			bSeenNonWhitespace = true;
			continue;
		}

		// 单行注释
		{
			bool bMatched = false;
			for (const FString& Prefix : Lang.LineCommentPrefixes)
			{
				if (MatchAt(Line, i, Prefix))
				{
					bMatched = true;
					break;
				}
			}
			if (bMatched)
			{
				Emit(ETokenKind::Comment, i, Len);
				return;
			}
		}

		// 预处理指令：行首 # + 标识符
		if (Lang.bPreprocessorDirectives && C == TEXT('#') && !bSeenNonWhitespace)
		{
			int32 End = i + 1;
			while (End < Len && Line[End] == TEXT(' ')) { ++End; }
			while (End < Len && IsIdentifierChar(Line[End])) { ++End; }
			Emit(ETokenKind::Preprocessor, i, End);
			i = End;
			bSeenNonWhitespace = true;
			continue;
		}

		bSeenNonWhitespace = true;

		// 字符串
		if (Lang.StringDelimiters.Contains(FString::Chr(C)))
		{
			int32 End = i + 1;
			while (End < Len)
			{
				if (Line[End] == TEXT('\\') && End + 1 < Len)
				{
					End += 2;
					continue;
				}
				if (Line[End] == C)
				{
					++End;
					break;
				}
				++End;
			}
			Emit(ETokenKind::String, i, End);
			i = End;
			continue;
		}

		// 数字
		if (FChar::IsDigit(C) || (C == TEXT('.') && i + 1 < Len && FChar::IsDigit(Line[i + 1])))
		{
			int32 End = i + 1;
			while (End < Len && (FChar::IsAlnum(Line[End]) || Line[End] == TEXT('.') || Line[End] == TEXT('_'))) { ++End; }
			Emit(ETokenKind::Number, i, End);
			i = End;
			continue;
		}

		// 标识符 / 关键字
		if (IsIdentifierChar(C) || C == TEXT('-') || C == TEXT('$') || C == TEXT('@'))
		{
			// '-' 仅在后面紧跟字母时按标识符处理（PowerShell -eq / 命令行开关），否则是运算符
			if (C == TEXT('-') && !(i + 1 < Len && FChar::IsAlpha(Line[i + 1])))
			{
				Emit(ETokenKind::Operator, i, i + 1);
				++i;
				continue;
			}

			int32 End = i + 1;
			while (End < Len)
			{
				if (IsIdentifierChar(Line[End]))
				{
					++End;
				}
				// 大小写不敏感的脚本类语言（PowerShell / Shell）允许 Write-Host 这类带连字符的命令名
				else if (!Lang.bCaseSensitive && Line[End] == TEXT('-') && End + 1 < Len && FChar::IsAlpha(Line[End + 1]))
				{
					End += 2;
				}
				else
				{
					break;
				}
			}

			const FString Word = Line.Mid(i, End - i);
			const FString LookupWord = Lang.bCaseSensitive ? Word : Word.ToLower();
			ETokenKind Kind = ETokenKind::Normal;
			if (KeywordSet.Contains(LookupWord))
			{
				Kind = ETokenKind::Keyword;
			}
			else if (TypeSet.Contains(LookupWord))
			{
				Kind = ETokenKind::Type;
			}
			Emit(Kind, i, End);
			i = End;
			continue;
		}

		// 其余单字符按运算符
		Emit(ETokenKind::Operator, i, i + 1);
		++i;
	}
}

void FTextFileSyntaxHighlighter::SetText(const FString& SourceString, FTextLayout& TargetTextLayout)
{
	TArray<FTextRange> LineRanges;
	FTextRange::CalculateLineRangesFromString(SourceString, LineRanges);

	TArray<FTextLayout::FNewLineData> LinesToAdd;
	LinesToAdd.Reserve(LineRanges.Num());

	const FTextBlockStyle& NormalStyle = GetStyle(ETokenKind::Normal);

	bool bInBlockComment = false;
	TArray<FToken> Tokens;

	for (const FTextRange& LineRange : LineRanges)
	{
		TSharedRef<FString> LineText = MakeShared<FString>(SourceString.Mid(LineRange.BeginIndex, LineRange.Len()));
		TArray<TSharedRef<IRun>> Runs;

		if (LineText->IsEmpty())
		{
			Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, NormalStyle));
		}
		else
		{
			TokenizeLine(*LineText, bInBlockComment, Tokens);
			Runs.Reserve(Tokens.Num());
			for (const FToken& Token : Tokens)
			{
				const FTextBlockStyle& Style = GetStyle(Token.Kind);
				if (Token.Kind == ETokenKind::Whitespace)
				{
					Runs.Add(FTabAwareTextRun::Create(FRunInfo(), LineText, Style, Token.Range, TabSize));
				}
				else
				{
					Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, Style, Token.Range));
				}
			}
		}

		LinesToAdd.Emplace(MoveTemp(LineText), MoveTemp(Runs));
	}

	TargetTextLayout.AddLines(LinesToAdd);
}
