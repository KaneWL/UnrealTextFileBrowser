// Copyright (C) 2026 Kane. All Rights Reserved.

#include "TextFileBrowserSettings.h"

#include "Misc/Paths.h"

namespace
{
	TArray<FString> Split(const TCHAR* InSpaceSeparated)
	{
		TArray<FString> Out;
		FString(InSpaceSeparated).ParseIntoArrayWS(Out);
		return Out;
	}

	FTextFileLanguage MakeCLike(const TCHAR* Name, const TCHAR* Extensions, const TCHAR* Keywords, const TCHAR* Types, bool bPreprocessor)
	{
		FTextFileLanguage Lang;
		Lang.Name = Name;
		Lang.Extensions = Split(Extensions);
		Lang.Keywords = Split(Keywords);
		Lang.Types = Split(Types);
		Lang.LineCommentPrefixes = { TEXT("//") };
		Lang.BlockCommentStart = TEXT("/*");
		Lang.BlockCommentEnd = TEXT("*/");
		Lang.StringDelimiters = TEXT("\"'");
		Lang.bPreprocessorDirectives = bPreprocessor;
		Lang.IndentTriggers = { TEXT("{"), TEXT("("), TEXT("[") };
		return Lang;
	}
}

UTextFileBrowserSettings::UTextFileBrowserSettings()
{
	TextExtensions = {
		// 纯文本 / 文档
		TEXT("txt"), TEXT("md"), TEXT("markdown"), TEXT("log"), TEXT("rst"),
		// C / C++
		TEXT("h"), TEXT("hpp"), TEXT("hh"), TEXT("inl"), TEXT("inc"),
		TEXT("c"), TEXT("cpp"), TEXT("cc"), TEXT("cxx"),
		// C# / 脚本
		TEXT("cs"), TEXT("lua"), TEXT("py"), TEXT("js"), TEXT("ts"),
		TEXT("bat"), TEXT("cmd"), TEXT("ps1"), TEXT("sh"),
		// 数据 / 配置
		TEXT("json"), TEXT("json5"), TEXT("xml"), TEXT("yaml"), TEXT("yml"),
		TEXT("ini"), TEXT("cfg"), TEXT("toml"), TEXT("csv"), TEXT("tsv"), TEXT("sql"),
		// Shader
		TEXT("hlsl"), TEXT("glsl"), TEXT("usf"), TEXT("ush"),
		// UE 描述文件
		TEXT("uplugin"), TEXT("uproject"),
	};

	CreatableExtensions = {
		TEXT("txt"), TEXT("md"), TEXT("json"), TEXT("lua"), TEXT("csv"), TEXT("ini"),
	};

	// ---------- 语言规则 ----------

	Languages.Add(MakeCLike(TEXT("C++"), TEXT("h hpp hh inl inc c cpp cc cxx"),
		TEXT("alignas alignof asm auto break case catch class concept const consteval constexpr constinit const_cast continue co_await co_return co_yield "
			"decltype default delete do dynamic_cast else enum explicit export extern false final for friend goto if inline mutable namespace new noexcept "
			"nullptr operator override private protected public register reinterpret_cast requires return sizeof static static_assert static_cast struct "
			"switch template this thread_local throw true try typedef typeid typename union using virtual volatile while "
			"UCLASS USTRUCT UENUM UFUNCTION UPROPERTY UINTERFACE UMETA UPARAM GENERATED_BODY GENERATED_UCLASS_BODY GENERATED_USTRUCT_BODY "
			"DECLARE_DELEGATE DECLARE_MULTICAST_DELEGATE DECLARE_DYNAMIC_MULTICAST_DELEGATE TEXT LOCTEXT NSLOCTEXT check checkf ensure ensureMsgf verify"),
		TEXT("bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t "
			"int8 int16 int32 int64 uint8 uint16 uint32 uint64 SIZE_T TCHAR ANSICHAR WIDECHAR "
			"FString FName FText FVector FVector2D FVector4 FRotator FQuat FTransform FColor FLinearColor FMatrix FBox FGuid FDateTime FTimespan "
			"TArray TMap TSet TSharedPtr TSharedRef TWeakPtr TUniquePtr TObjectPtr TWeakObjectPtr TSoftObjectPtr TSoftClassPtr TSubclassOf TOptional TFunction TPair "
			"UObject UClass UStruct UFunction UProperty AActor APawn ACharacter UActorComponent USceneComponent UWorld UGameInstance UPrimitiveComponent FArchive"),
		true));

	Languages.Add(MakeCLike(TEXT("C#"), TEXT("cs"),
		TEXT("abstract as base break case catch checked class const continue default delegate do else enum event explicit extern false finally fixed for "
			"foreach goto if implicit in interface internal is lock namespace new null operator out override params private protected public readonly ref "
			"return sealed sizeof stackalloc static struct switch this throw true try typeof unchecked unsafe using virtual volatile while "
			"add alias ascending async await descending dynamic from get global group init into join let nameof not notnull on orderby partial record remove "
			"select set unmanaged value var when where with yield"),
		TEXT("bool byte char decimal double float int long object sbyte short string uint ulong ushort void "
			"List Dictionary HashSet Queue Stack Task Action Func IEnumerable IList IDictionary Nullable Span Memory StringBuilder Console Math"),
		true));

	Languages.Add(MakeCLike(TEXT("HLSL"), TEXT("hlsl glsl usf ush"),
		TEXT("break case cbuffer centroid class column_major compile const continue default discard do else export extern for groupshared if in inline inout "
			"interface line lineadj linear matrix namespace nointerpolation noperspective out packoffset pass pixelfragment point precise register return "
			"row_major sample sampler shared stateblock static struct switch tbuffer technique texture triangle triangleadj typedef uniform unorm snorm "
			"vertexfragment void volatile while true false"),
		TEXT("bool int uint half float double dword min16float min10float min16int min12int min16uint "
			"float2 float3 float4 float2x2 float3x3 float4x4 float3x4 float4x3 half2 half3 half4 int2 int3 int4 uint2 uint3 uint4 bool2 bool3 bool4 "
			"matrix vector Texture1D Texture2D Texture3D TextureCube Texture2DArray TextureCubeArray Texture2DMS SamplerState SamplerComparisonState "
			"Buffer StructuredBuffer RWBuffer RWStructuredBuffer RWTexture2D RWTexture3D ByteAddressBuffer RWByteAddressBuffer "
			"SV_Position SV_Target SV_Depth SV_VertexID SV_InstanceID SV_DispatchThreadID SV_GroupID SV_GroupThreadID SV_GroupIndex"),
		true));

	Languages.Add(MakeCLike(TEXT("JavaScript / TypeScript"), TEXT("js ts"),
		TEXT("abstract as async await break case catch class const continue debugger declare default delete do else enum export extends false finally for "
			"from function get if implements import in instanceof interface is keyof let module namespace new null of package private protected public "
			"readonly return set static super switch this throw true try type typeof undefined var void while with yield"),
		TEXT("any boolean number string symbol bigint object never unknown Array Promise Map Set Date RegExp Error Math JSON console"),
		false));

	{
		FTextFileLanguage Lua;
		Lua.Name = TEXT("Lua");
		Lua.Extensions = { TEXT("lua") };
		Lua.Keywords = Split(TEXT("and break do else elseif end false for function goto if in local nil not or repeat return then true until while self"));
		Lua.Types = Split(TEXT("print pairs ipairs type tostring tonumber require pcall xpcall error assert select unpack rawget rawset rawequal setmetatable "
			"getmetatable next load loadstring dofile collectgarbage string table math os io coroutine debug utf8 _G _ENV"));
		Lua.LineCommentPrefixes = { TEXT("--") };
		Lua.BlockCommentStart = TEXT("--[[");
		Lua.BlockCommentEnd = TEXT("]]");
		Lua.StringDelimiters = TEXT("\"'");
		Lua.IndentTriggers = { TEXT("then"), TEXT("do"), TEXT("else"), TEXT("repeat"), TEXT("{"), TEXT("(") };
		Languages.Add(Lua);
	}

	{
		FTextFileLanguage Py;
		Py.Name = TEXT("Python");
		Py.Extensions = { TEXT("py") };
		Py.Keywords = Split(TEXT("False None True and as assert async await break class continue def del elif else except finally for from global if import "
			"in is lambda nonlocal not or pass raise return try while with yield self cls"));
		Py.Types = Split(TEXT("int float str bool list dict set tuple bytes object type range len print open isinstance super enumerate zip map filter "
			"sorted reversed min max sum abs round any all input repr hash id getattr setattr hasattr Exception ValueError TypeError KeyError"));
		Py.LineCommentPrefixes = { TEXT("#") };
		Py.StringDelimiters = TEXT("\"'");
		Py.IndentTriggers = { TEXT(":"), TEXT("("), TEXT("["), TEXT("{") };
		Languages.Add(Py);
	}

	{
		FTextFileLanguage Json;
		Json.Name = TEXT("JSON");
		Json.Extensions = { TEXT("json"), TEXT("json5"), TEXT("uplugin"), TEXT("uproject") };
		Json.Keywords = Split(TEXT("true false null"));
		Json.LineCommentPrefixes = { TEXT("//") };
		Json.BlockCommentStart = TEXT("/*");
		Json.BlockCommentEnd = TEXT("*/");
		Json.StringDelimiters = TEXT("\"");
		Json.IndentTriggers = { TEXT("{"), TEXT("[") };
		Languages.Add(Json);
	}

	{
		FTextFileLanguage Ini;
		Ini.Name = TEXT("INI / Config");
		Ini.Extensions = { TEXT("ini"), TEXT("cfg"), TEXT("toml") };
		Ini.Keywords = Split(TEXT("true false True False TRUE FALSE None"));
		Ini.LineCommentPrefixes = { TEXT(";"), TEXT("#") };
		Ini.StringDelimiters = TEXT("\"");
		Ini.bCaseSensitive = false;
		Languages.Add(Ini);
	}

	{
		FTextFileLanguage Yaml;
		Yaml.Name = TEXT("YAML");
		Yaml.Extensions = { TEXT("yaml"), TEXT("yml") };
		Yaml.Keywords = Split(TEXT("true false null yes no on off ~"));
		Yaml.LineCommentPrefixes = { TEXT("#") };
		Yaml.StringDelimiters = TEXT("\"'");
		Yaml.bCaseSensitive = false;
		Yaml.IndentTriggers = { TEXT(":") };
		Languages.Add(Yaml);
	}

	{
		FTextFileLanguage Xml;
		Xml.Name = TEXT("XML");
		Xml.Extensions = { TEXT("xml") };
		Xml.BlockCommentStart = TEXT("<!--");
		Xml.BlockCommentEnd = TEXT("-->");
		Xml.StringDelimiters = TEXT("\"'");
		Languages.Add(Xml);
	}

	{
		FTextFileLanguage Shell;
		Shell.Name = TEXT("Shell / Batch");
		Shell.Extensions = { TEXT("sh"), TEXT("bat"), TEXT("cmd") };
		Shell.Keywords = Split(TEXT("if then else elif fi for while do done case esac in function return exit export local echo set setlocal endlocal "
			"goto call rem pause exist not defined errorlevel equ neq lss leq gtr geq"));
		Shell.LineCommentPrefixes = { TEXT("#"), TEXT("::"), TEXT("REM "), TEXT("rem ") };
		Shell.StringDelimiters = TEXT("\"'");
		Shell.bCaseSensitive = false;
		Shell.IndentTriggers = { TEXT("then"), TEXT("do"), TEXT("else"), TEXT("(") };
		Languages.Add(Shell);
	}

	{
		FTextFileLanguage Ps;
		Ps.Name = TEXT("PowerShell");
		Ps.Extensions = { TEXT("ps1") };
		Ps.Keywords = Split(TEXT("begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for foreach from function "
			"hidden if in inlinescript param process return static switch throw trap try until using var while workflow -eq -ne -gt -ge -lt -le -and -or -not"));
		Ps.Types = Split(TEXT("Write-Host Write-Output Get-ChildItem Get-Content Set-Content Get-Item Test-Path Join-Path Split-Path Select-Object "
			"Where-Object ForEach-Object Remove-Item Copy-Item Move-Item New-Item Out-File Import-Module Export-ModuleMember"));
		Ps.LineCommentPrefixes = { TEXT("#") };
		Ps.BlockCommentStart = TEXT("<#");
		Ps.BlockCommentEnd = TEXT("#>");
		Ps.StringDelimiters = TEXT("\"'");
		Ps.bCaseSensitive = false;
		Ps.IndentTriggers = { TEXT("{"), TEXT("(") };
		Languages.Add(Ps);
	}

	{
		FTextFileLanguage Sql;
		Sql.Name = TEXT("SQL");
		Sql.Extensions = { TEXT("sql") };
		Sql.Keywords = Split(TEXT("select from where insert into values update set delete create table drop alter add index view join inner left right outer "
			"on as and or not null is in exists between like group by order having limit offset union all distinct primary key foreign references "
			"begin commit rollback transaction if else case when then end"));
		Sql.Types = Split(TEXT("int integer bigint smallint tinyint float real double decimal numeric char varchar text blob date datetime timestamp boolean"));
		Sql.LineCommentPrefixes = { TEXT("--") };
		Sql.BlockCommentStart = TEXT("/*");
		Sql.BlockCommentEnd = TEXT("*/");
		Sql.StringDelimiters = TEXT("'\"");
		Sql.bCaseSensitive = false;
		Languages.Add(Sql);
	}

	{
		FTextFileLanguage Md;
		Md.Name = TEXT("Markdown");
		Md.Extensions = { TEXT("md"), TEXT("markdown"), TEXT("rst") };
		Md.StringDelimiters = TEXT("`");
		Md.bPreprocessorDirectives = true;
		Languages.Add(Md);
	}

	ExtraMounts.Add({"/ProjectScript","Script"});
	ExtraMounts.Add({"/ProjectShaders","Shaders"});
}

const FTextFileLanguage* UTextFileBrowserSettings::FindLanguageForFile(const FString& InFilename) const
{
	const FString Ext = FPaths::GetExtension(InFilename);
	if (Ext.IsEmpty())
	{
		return nullptr;
	}

	for (const FTextFileLanguage& Lang : Languages)
	{
		for (const FString& LangExt : Lang.Extensions)
		{
			if (LangExt.Equals(Ext, ESearchCase::IgnoreCase))
			{
				return &Lang;
			}
		}
	}
	return nullptr;
}

FString UTextFileBrowserSettings::GetIndentUnit() const
{
	return bInsertSpacesForTab ? FString::ChrN(FMath::Max(1, TabSize), TEXT(' ')) : FString(TEXT("\t"));
}

#if WITH_EDITOR
void UTextFileBrowserSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		OnSettingsChanged.Broadcast();
	}
}
#endif
