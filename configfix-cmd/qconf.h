/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */

#include <QCheckBox>
#include <QDialog>
#include <QHeaderView>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QList>
#include <QComboBox>
#include <QLabel>
#include <qstring.h>
#include <thread>
#include <condition_variable>

#include "expr.h"

#include "configfix.h"

class ConfigView;
class ConfigList;
class ConfigItem;
class ConfigLineEdit;
class ConfigMainWindow;

class ConfigSettings : public QSettings {
public:
	ConfigSettings();
	QList<int> readSizes(const QString& key, bool *ok);
	bool writeSizes(const QString& key, const QList<int>& value);
};

enum colIdx {
	promptColIdx, nameColIdx, noColIdx, modColIdx, yesColIdx, dataColIdx
};
enum listMode {
	singleMode, menuMode, symbolMode, fullMode, listMode
};
enum optionMode {
	normalOpt = 0, allOpt, promptOpt
};

enum symbolStatus {
	UNSATISFIED, SATISFIED
};

typedef struct {
	QString symbol;
	QString change_needed;
	enum symbolStatus status;
	tristate change_requested;
} Constraint;

QString tristate_value_to_string(tristate val);
tristate string_value_to_tristate(QString s);

class ConfigList : public QTreeWidget {
	Q_OBJECT
	typedef class QTreeWidget Parent;
public:
	ConfigList(ConfigView* p, const char *name = 0);
	void reinit(void);
	ConfigItem* findConfigItem(struct menu *);
	ConfigView* parent(void) const
	{
		return (ConfigView*)Parent::parent();
	}
	void setSelected(QTreeWidgetItem *item, bool enable) {
		for (int i = 0; i < selectedItems().size(); i++)
			selectedItems().at(i)->setSelected(false);

		item->setSelected(enable);
	}

protected:
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseDoubleClickEvent(QMouseEvent *e);
	void focusInEvent(QFocusEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

public slots:
	void setRootMenu(struct menu *menu);

	void updateList();
	void setValue(ConfigItem* item, tristate val);
	void changeValue(ConfigItem* item);
	void updateSelection(void);
	void saveSettings(void);
	void setOptionMode(QAction *action);

signals:
	void menuChanged(struct menu *menu);
	void menuSelected(struct menu *menu);
	void itemSelected(struct menu *menu);
	void parentSelected(void);
	void gotFocus(struct menu *);
	void selectedChanged(QList<QTreeWidgetItem*> selection);
	void UpdateConflictsViewColorization();

public:
	void updateListAll(void)
	{
		updateAll = true;
		updateList();
		updateAll = false;
	}
	void setAllOpen(bool open);
	void setParentMenu(void);

	bool menuSkip(struct menu *);

	void updateMenuList(ConfigItem *parent, struct menu*);
	void updateMenuList(struct menu *menu);

	bool updateAll;

	bool showName, showRange, showData;
	enum listMode mode;
	enum optionMode optMode;
	struct menu *rootEntry;
	QPalette disabledColorGroup;
	QPalette inactivedColorGroup;
	QMenu* headerPopup;

	static QAction *showNormalAction, *showAllAction, *showPromptAction, *addSymbolsFromContextMenu;
};

class ConfigItem : public QTreeWidgetItem {
	typedef class QTreeWidgetItem Parent;
public:
	ConfigItem(ConfigList *parent, ConfigItem *after, struct menu *m, bool v)
	: Parent(parent, after), nextItem(0), menu(m), visible(v), goParent(false)
	{
		init();
	}
	ConfigItem(ConfigItem *parent, ConfigItem *after, struct menu *m, bool v)
	: Parent(parent, after), nextItem(0), menu(m), visible(v), goParent(false)
	{
		init();
	}
	ConfigItem(ConfigList *parent, ConfigItem *after, bool v)
	: Parent(parent, after), nextItem(0), menu(0), visible(v), goParent(true)
	{
		init();
	}
	~ConfigItem(void);
	void init(void);
	void okRename(int col);
	void updateMenu(void);
	void testUpdateMenu(bool v);
	ConfigList* listView() const
	{
		return (ConfigList*)Parent::treeWidget();
	}
	ConfigItem* firstChild() const
	{
		return (ConfigItem *)Parent::child(0);
	}
	ConfigItem* nextSibling()
	{
		ConfigItem *ret = NULL;
		ConfigItem *_parent = (ConfigItem *)parent();

		if(_parent) {
			ret = (ConfigItem *)_parent->child(_parent->indexOfChild(this)+1);
		} else {
			QTreeWidget *_treeWidget = treeWidget();
			ret = (ConfigItem *)_treeWidget->topLevelItem(_treeWidget->indexOfTopLevelItem(this)+1);
		}

		return ret;
	}
	// TODO: Implement paintCell

	ConfigItem* nextItem;
	struct menu *menu;
	bool visible;
	bool goParent;

	static QIcon symbolYesIcon, symbolModIcon, symbolNoIcon;
	static QIcon choiceYesIcon, choiceNoIcon;
	static QIcon menuIcon, menubackIcon;
};

class ConfigLineEdit : public QLineEdit {
	Q_OBJECT
	typedef class QLineEdit Parent;
public:
	ConfigLineEdit(ConfigView* parent);
	ConfigView* parent(void) const
	{
		return (ConfigView*)Parent::parent();
	}
	void show(ConfigItem *i);
	void keyPressEvent(QKeyEvent *e);

public:
	ConfigItem *item;
};

class ConfigView : public QWidget {
	Q_OBJECT
	typedef class QWidget Parent;
public:
	ConfigView(QWidget* parent, const char *name = 0);
	~ConfigView(void);
	static void updateList();
	static void updateListAll(void);

	bool showName(void) const { return list->showName; }
	bool showRange(void) const { return list->showRange; }
	bool showData(void) const { return list->showData; }
public slots:
	void setShowName(bool);
	void setShowRange(bool);
	void setShowData(bool);

	void ShowContextMenu(const QPoint& p);
signals:
	void showNameChanged(bool);
	void showRangeChanged(bool);
	void showDataChanged(bool);
public:
	ConfigList* list;
	ConfigLineEdit* lineEdit;

	static ConfigView* viewList;
	ConfigView* nextView;

	static QAction *showNormalAction;
	static QAction *showAllAction;
	static QAction *showPromptAction;
	static QAction *addSymbolsFromContextMenu;
};
class ConflictsView : public QWidget {
	ConfigLineEdit* lineEdit;
	Q_OBJECT
	typedef class QWidget Parent;
public:
	ConflictsView(QWidget* parent, const char *name = 0);
	void addSymbol(struct menu * m);
	int current_solution_number = -1;

public slots:
	void cellClicked(int, int);
	void addSymbol();
	void addSymbolFromContextMenu();
	void removeSymbol();
	void menuChanged1(struct menu *);
	void changeToNo();
	void changeToYes();
	void changeToModule();
	void selectedChanged(QList<QTreeWidgetItem*> selection);
	void applyFixButtonClick();
	void UpdateConflictsViewColorization();
	void updateResults();
	void changeSolutionTable(int solution_number);
	void calculateFixes();
signals:
	void showNameChanged(bool);
	void showRangeChanged(bool);
	void showDataChanged(bool);
	void conflictSelected(struct menu *);
	void refreshMenu();
	void resultsReady();
public:
	QTableWidget* conflictsTable;
	QList<Constraint> constraints;
	QComboBox* solutionSelector{nullptr};
	QTableWidget* solutionTable{nullptr};
	QPushButton* applyFixButton{nullptr};
	struct sfl_list * solution_output{nullptr};
	QToolBar *conflictsToolBar;
	struct menu * currentSelectedMenu ;
	QLabel* numSolutionLabel{nullptr};
	QList<QTreeWidgetItem*> currentSelection;
	QAction *fixConflictsAction_{nullptr};
	void runSatConfAsync();
	std::thread* runSatConfAsyncThread{nullptr};
	std::mutex satconf_mutex;
	std::condition_variable satconf_cancellation_cv;
	bool satconf_cancelled{false};
};

class ConfigInfoView : public QTextBrowser {
	Q_OBJECT
	typedef class QTextBrowser Parent;
	QMenu *contextMenu;
public:
	ConfigInfoView(QWidget* parent, const char *name = 0);
	bool showDebug(void) const { return _showDebug; }

public slots:
	void setInfo(struct menu *menu);
	void saveSettings(void);
	void setShowDebug(bool);
	void clicked (const QUrl &url);

signals:
	void showDebugChanged(bool);
	void menuSelected(struct menu *);

protected:
	void symbolInfo(void);
	void menuInfo(void);
	QString debug_info(struct symbol *sym);
	static QString print_filter(const QString &str);
	static void expr_print_help(void *data, struct symbol *sym, const char *str);
	void contextMenuEvent(QContextMenuEvent *event);

	struct symbol *sym;
	struct menu *_menu;
	bool _showDebug;
};

class ConfigSearchWindow : public QDialog {
	Q_OBJECT
	typedef class QDialog Parent;
public:
	ConfigSearchWindow(ConfigMainWindow *parent);

public slots:
	void saveSettings(void);
	void search(void);
	void UpdateConflictsViewColorizationFowarder();
signals:
	void UpdateConflictsViewColorization();

protected:
	QLineEdit* editField;
	QPushButton* searchButton;
	QSplitter* split;
	ConfigView* list;
	ConfigInfoView* info;

	struct symbol **result;
};


class ConfigMainWindow : public QMainWindow {
	Q_OBJECT

	char *configname;
	static QAction *saveAction;
	static void conf_changed(void);
public:
	ConfigMainWindow(void);
public slots:
	void changeMenu(struct menu *);
	void changeItens(struct menu *);
	void setMenuLink(struct menu *);
	void listFocusChanged(void);
	void goBack(void);
	void loadConfig(void);
	bool saveConfig(void);
	void saveConfigAs(void);
	void searchConfig(void);
	void showSingleView(void);
	void showSplitView(void);
	void showFullView(void);
	void showIntro(void);
	void showAbout(void);
	void saveSettings(void);
	void conflictSelected(struct menu *);
	void refreshMenu();

protected:
	void closeEvent(QCloseEvent *e);

	ConfigSearchWindow *searchWindow;
	ConfigView *menuView;
	ConfigList *menuList;
	ConfigView *configView;
	ConfigList *configList;
	ConfigInfoView *helpText;
	ConflictsView *conflictsView;
	QToolBar *toolBar;
	QToolBar *conflictsToolBar;
	QAction *backAction;
	QAction *singleViewAction;
	QAction *splitViewAction;
	QAction *fullViewAction;
	QSplitter *split1;
	QSplitter *split2;
	QSplitter *split3;
};

class droppableView : public QTableWidget
{
public:
	droppableView(QWidget *parent = nullptr) {}
protected:
	void dropEvent(QDropEvent *event);
};
