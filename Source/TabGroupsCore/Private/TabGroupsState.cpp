// Copyright 2026 Jot. All Rights Reserved.

#include "TabGroupsState.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace TabGroups
{
	namespace
	{
		constexpr int32 StateVersion = 1;

		TArray<TSharedPtr<FJsonValue>> ToJsonStrings(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const FString& Value : Values)
			{
				Out.Add(MakeShared<FJsonValueString>(Value));
			}
			return Out;
		}

		TArray<FString> FromJsonStrings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
		{
			TArray<FString> Out;
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (Object->TryGetArrayField(Field, Values))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					FString Text;
					if (Value.IsValid() && Value->TryGetString(Text))
					{
						Out.Add(Text);
					}
				}
			}
			return Out;
		}

		TSharedRef<FJsonObject> LayoutToJson(const FSavedLayout& Layout)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Groups;
			for (const FSavedGroup& Group : Layout.Groups)
			{
				TSharedRef<FJsonObject> GroupObject = MakeShared<FJsonObject>();
				GroupObject->SetStringField(TEXT("id"), Group.Id);
				GroupObject->SetStringField(TEXT("name"), Group.Name);
				GroupObject->SetNumberField(TEXT("color"), Group.Color);
				GroupObject->SetBoolField(TEXT("collapsed"), Group.bCollapsed);
				GroupObject->SetArrayField(TEXT("members"), ToJsonStrings(Group.Members));
				Groups.Add(MakeShared<FJsonValueObject>(GroupObject));
			}
			Object->SetArrayField(TEXT("groups"), Groups);
			Object->SetArrayField(TEXT("pinned"), ToJsonStrings(Layout.Pinned));
			return Object;
		}

		void LayoutFromJson(const TSharedPtr<FJsonObject>& Object, FSavedLayout& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
			if (Object->TryGetArrayField(TEXT("groups"), Groups))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Groups)
				{
					const TSharedPtr<FJsonObject>* GroupObject = nullptr;
					if (!Value.IsValid() || !Value->TryGetObject(GroupObject))
					{
						continue;
					}
					FSavedGroup Group;
					(*GroupObject)->TryGetStringField(TEXT("id"), Group.Id);
					(*GroupObject)->TryGetStringField(TEXT("name"), Group.Name);
					(*GroupObject)->TryGetNumberField(TEXT("color"), Group.Color);
					(*GroupObject)->TryGetBoolField(TEXT("collapsed"), Group.bCollapsed);
					Group.Color = FMath::Clamp(Group.Color, 0, NumGroupColors - 1);
					Group.Members = FromJsonStrings(*GroupObject, TEXT("members"));
					if (Group.Members.Num() > 0)
					{
						Out.Groups.Add(MoveTemp(Group));
					}
				}
			}
			Out.Pinned = FromJsonStrings(Object, TEXT("pinned"));
		}
	}

	FString SerializeState(const FSavedState& State)
	{
		TSharedRef<FJsonObject> Root = LayoutToJson(State.Layout);
		Root->SetNumberField(TEXT("version"), StateVersion);
		Root->SetStringField(TEXT("activeWorkspace"), State.ActiveWorkspace);

		TArray<TSharedPtr<FJsonValue>> Workspaces;
		for (const FSavedWorkspace& Workspace : State.Workspaces)
		{
			TSharedRef<FJsonObject> Object = LayoutToJson(Workspace.Layout);
			Object->SetStringField(TEXT("name"), Workspace.Name);
			Object->SetArrayField(TEXT("assets"), ToJsonStrings(Workspace.Assets));
			Workspaces.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetArrayField(TEXT("workspaces"), Workspaces);

		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	bool DeserializeState(const FString& Json, FSavedState& OutState)
	{
		OutState = FSavedState();

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}

		LayoutFromJson(Root, OutState.Layout);
		Root->TryGetStringField(TEXT("activeWorkspace"), OutState.ActiveWorkspace);

		const TArray<TSharedPtr<FJsonValue>>* Workspaces = nullptr;
		if (Root->TryGetArrayField(TEXT("workspaces"), Workspaces))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Workspaces)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(Object))
				{
					continue;
				}
				FSavedWorkspace Workspace;
				(*Object)->TryGetStringField(TEXT("name"), Workspace.Name);
				LayoutFromJson(*Object, Workspace.Layout);
				Workspace.Assets = FromJsonStrings(*Object, TEXT("assets"));
				if (!Workspace.Name.IsEmpty())
				{
					OutState.Workspaces.Add(MoveTemp(Workspace));
				}
			}
		}
		return true;
	}
}
