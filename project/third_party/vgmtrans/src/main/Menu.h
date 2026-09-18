#pragma once

#include "common.h"
#include "VGMItem.h"

template <class T>
class Menu
{
public:
	Menu() {}
	virtual ~Menu() {}

	void AddMenuItem(bool (T::*funcPtr)(void), const wchar_t* name, uint8_t flag = 0)
	{
		funcs.push_back(funcPtr);
		names.push_back(name);
	}

	bool CallMenuItem(VGMItem* item, int menuItemNum)
	{
		return (((T*)item)->*funcs[menuItemNum])();
	}

	std::vector<const wchar_t*>* GetMenuItemNames(void)
	{
		return &names;
	}

protected:
	std::vector<const wchar_t*> names;
	std::vector<bool (T::*)(void)> funcs;
};

#define BEGIN_MENU_SUB(origclass, parentclass)                                                public:                                                                                       template <class T> class origclass##_Menu : public parentclass::parentclass##_Menu<T>         {                                                                                         public:                                                                                       origclass##_Menu()                                                                        {

#define BEGIN_MENU(origclass)                                                                 public:                                                                                       template <class T> class origclass##_Menu : public Menu<T>                                {                                                                                         public:                                                                                       origclass##_Menu()                                                                        {

#define MENU_ITEM(origclass, func, menutext)    Menu<T>::AddMenuItem(&origclass::func, menutext);
#define END_MENU()    } };

#define DECLARE_MENU(origclass)

