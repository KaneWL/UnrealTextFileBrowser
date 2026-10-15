// Copyright (C) 2026 Kane. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Styling/SlateTypes.h"
#include "TextFileBrowserSettings.h"

/** 按 FTextFileLanguage 规则做词法着色的 Marshaller；语言为空时退化为纯文本（仅做 Tab 宽度处理） */
class FTextFileSyntaxHighlighter : public FPlainTextLayoutMarshaller
{
public:
	enum class ETokenKind : uint8
	{
		Normal,
		Keyword,
		Type,
		String,
		Number,
		Comment,
		Preprocessor,
		Operator,
		Whitespace,
	};

	struct FToken
	{
		ETokenKind Kind;
		FTextRange Range;
	};

	static TSharedRef<FTextFileSyntaxHighlighter> Create(const FTextFileLanguage* InLanguage, const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont, int32 InTabSize);

	/** 字体 / 配色改变后调用；会在下一帧触发重新着色 */
	void UpdateStyle(const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont, int32 InTabSize);

	void SetHighlightEnabled(bool bInEnabled);
	bool IsHighlightEnabled() const { return bHighlightEnabled; }

	const FTextFileLanguage* GetLanguage() const { return Language.GetPtrOrNull(); }

	/** 对一行做词法切分。bInOutBlockComment 跨行携带块注释状态 */
	void TokenizeLine(const FString& InLine, bool& bInOutBlockComment, TArray<FToken>& OutTokens) const;

	/** 是否标识符字符（含非 ASCII 字符，便于中文变量名/文本） */
	static bool IsIdentifierChar(TCHAR C)
	{
		return FChar::IsAlnum(C) || C == TEXT('_') || C > 127;
	}

	// ITextLayoutMarshaller
	virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override;
	virtual bool RequiresLiveUpdate() const override { return true; }

protected:
	FTextFileSyntaxHighlighter(const FTextFileLanguage* InLanguage, const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont, int32 InTabSize);

	void RebuildStyles(const FTextFileHighlightColors& InColors, const FSlateFontInfo& InFont);
	const FTextBlockStyle& GetStyle(ETokenKind Kind) const;

	TOptional<FTextFileLanguage> Language;
	/** 关键字 / 类型集合；大小写不敏感语言存小写 */
	TSet<FString> KeywordSet;
	TSet<FString> TypeSet;
	int32 TabSize;
	bool bHighlightEnabled = true;

	FTextBlockStyle Styles[9];
};
