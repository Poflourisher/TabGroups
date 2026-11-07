// Copyright 2026 Jot. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "TabGroupsLog.h"
#include "TabGroupsManager.h"

DEFINE_LOG_CATEGORY(LogTabGroups);

class FTabGroupsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Manager = MakeShared<FTabGroupsManager>();
		Manager->Startup();
	}

	virtual void ShutdownModule() override
	{
		Manager->Shutdown();
		Manager.Reset();
	}

private:
	TSharedPtr<FTabGroupsManager> Manager;
};

IMPLEMENT_MODULE(FTabGroupsModule, TabGroups)
