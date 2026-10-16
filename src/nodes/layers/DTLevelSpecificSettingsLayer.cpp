#include <nodes/layers/DTLevelSpecificSettingsLayer.hpp>

#include <utils/CCResizeTo.hpp>
#include <nodes/layers/DTLayer.hpp>

#include <nodes/optionNodes/RunOptions.hpp>
#include <nodes/optionNodes/LinkOptions.hpp>
#include <nodes/optionNodes/ModifyOptions.hpp>
#include <nodes/optionNodes/SaveOptions.hpp>
#include <nodes/optionNodes/SectionOptions.hpp>
#include <nodes/optionNodes/SessionOptions.hpp>
#include <nodes/optionNodes/EditorPages.hpp>
#include <editor/EditorManager.hpp>
#include <utils/Settings.hpp>

DTLevelSpecificSettingsLayer* DTLevelSpecificSettingsLayer::create() {
    auto popup = new DTLevelSpecificSettingsLayer;
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool DTLevelSpecificSettingsLayer::init() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    // @geode-ignore(unknown-resource)
    if (!Popup::init(winSize.width - 30, winSize.height - 30, "geode.loader/GE_square01.png"))
        return false;

    this->setTitle("Level Options", "goldFont.fnt", .8f, 15);

    // Editor Mode replaces the Modify tab with the Edit / GD / Sessions / Check tabs. There are more
    // tabs then, so the labels are smaller and the "|" separators are dropped to keep one row
    const bool editorMode = Settings::getEditorMode();
    const float tabScale = editorMode ? .72f : .85f;
    if (editorMode) EditorManager::resetSession();

    auto pages = CCMenu::create();
    pages->setLayout(RowLayout::create()
        ->setGap(editorMode ? 12 : 15)
        ->setGrowCrossAxis(true)
        ->setCrossAxisOverflow(false)
    );
    pages->setContentSize({m_size.width - 5, 25});
    pages->setPosition({m_size.width / 2, this->m_title->getPositionY() - this->m_title->getScaledContentHeight() / 2 - pages->getScaledContentHeight() / 2});
    m_mainLayer->addChild(pages);

    auto seperator = CCScale9Sprite::create("square.png");
    seperator->setContentSize({pages->getContentWidth(), 1});
    seperator->setScaleY(8 / pages->getContentHeight());
    seperator->setPosition({pages->getPositionX(), pages->getPositionY() - pages->getScaledContentHeight() / 2 - 5});
    seperator->setAnchorPoint({.5f, 1});
    m_mainLayer->addChild(seperator);

    optionUnderline = CCScale9Sprite::create("square.png");
    optionUnderline->setScaleY(.5f);
    optionUnderline->setContentHeight(1);
    m_mainLayer->addChild(optionUnderline);

    auto pageSize = ccp(m_size.width - 10, seperator->getPositionY() - 10);
    auto addSeperator = [&]() {
        if (!editorMode) pages->addChild(DTLevelSpecificSettingsLayer::createSeperator());
    };

    auto firstOption = DTLevelSpecificSettingsLayer::createPageBtn("Run", RunOptions::create(pageSize), tabScale);

    pages->addChild(firstOption);
    if (editorMode) {
        pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Edit", EditorEditPage::create(pageSize), tabScale));
        pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("GD", EditorGdPage::create(pageSize), tabScale));
        pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Sessions", EditorSessionsPage::create(pageSize), tabScale));
        pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Check", EditorCheckPage::create(pageSize), tabScale));
    }
    else {
        addSeperator();
        pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Modify", ModifyOptions::create(pageSize), tabScale));
    }
    addSeperator();
    pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Save", SaveOptions::create(pageSize), tabScale));
    addSeperator();
    pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Link", LinkOptions::create(pageSize), tabScale));
    addSeperator();
    pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Section", SectionOptions::create(pageSize), tabScale));
    // pages->addChild(DTLevelSpecificSettingsLayer::createSeperator());
    // pages->addChild(DTLevelSpecificSettingsLayer::createPageBtn("Session", SessionOptions::create(ccp(m_size.width - 10, seperator->getPositionY() - 10))));

    pages->updateLayout();
    
    sendUnderlineTo(firstOption, true);
    switchPage(firstOption);

    return true;
}

CCNode* DTLevelSpecificSettingsLayer::createPageBtn(const std::string& text, OptionsNode* page, float scale){

    auto spr = CCLabelBMFont::create(text.c_str(), "gjFont17.fnt", 50);
    spr->setScale(scale);
    auto button = CCMenuItemSpriteExtra::create(
        spr,
        this,
        menu_selector(DTLevelSpecificSettingsLayer::switchPage)
    );
    page->setSettingsLayer(this);
    pages.insert({button, page});

    if (page != nullptr){
        page->setZOrder(2);
        page->setPosition({5, 5});
        m_mainLayer->addChild(page);
    }

    return button;
}

CCNode* DTLevelSpecificSettingsLayer::createSeperator(){
    auto spr = CCLabelBMFont::create("|", "gjFont17.fnt");
    return spr;
}

void DTLevelSpecificSettingsLayer::sendUnderlineTo(CCNode* node, bool instant){
    optionUnderline->stopAllActions();

    float movementTime = instant ? 0 : .2f;

    CCPoint nodePosInSpace = optionUnderline->getParent()->convertToNodeSpace(node->getParent()->convertToWorldSpace(node->getPosition()));

    CCPoint leftSide = optionUnderline->getParent()->convertToNodeSpace(node->getParent()->convertToWorldSpace(node->getPosition() - node->getScaledContentSize() * node->getAnchorPoint()));
    CCPoint rightSide = optionUnderline->getParent()->convertToNodeSpace(node->getParent()->convertToWorldSpace(node->getPosition() + node->getScaledContentSize() * node->getAnchorPoint()));

    float localWidth = rightSide.x - leftSide.x;
    float localHeight = rightSide.y - leftSide.y;

    optionUnderline->runAction(
        CCEaseExponentialOut::create(
            CCMoveTo::create(
                movementTime,
                nodePosInSpace - ccp(0, localHeight / 2)
            )
        )
    );
    optionUnderline->runAction(
        CCSequence::create(
            CCEaseInOut::create(
                CCResizeTo::create(
                    movementTime,
                    {localWidth, optionUnderline->getContentHeight()}
                ),
                2
            ),
            CCCallFuncO::create(
                this,
                callfuncO_selector(DTLevelSpecificSettingsLayer::makeUnderlineIdle),
                optionUnderline
            ),
            nullptr
        )
    );
}

void DTLevelSpecificSettingsLayer::makeUnderlineIdle(CCObject* sender){
    auto node = static_cast<CCNode*>(sender);

    float scaleAmount = 2;

    optionUnderline->runAction(
        CCRepeatForever::create(
            CCSequence::create(
                CCEaseInOut::create(
                    CCResizeTo::create(
                        1,
                        {node->getContentWidth() + scaleAmount, optionUnderline->getContentHeight()}
                    ),
                    2
                ),
                CCEaseInOut::create(
                    CCResizeTo::create(
                        1,
                        {node->getContentWidth() - scaleAmount, optionUnderline->getContentHeight()}
                    ),
                    2
                ),
                nullptr
            )
        )
    );
}

void DTLevelSpecificSettingsLayer::switchPage(CCObject* sender){
    if (!pages.contains(sender)) return;

    auto senderNode = static_cast<CCNode*>(sender);

    if (currentPage == pages[sender]) return;

    DTLevelSpecificSettingsLayer::sendUnderlineTo(senderNode);

    if (currentPage != nullptr)
        currentPage->onClosed();

    currentPage = pages[sender];

    if (currentPage != nullptr)
        currentPage->onOpened();
}

void DTLevelSpecificSettingsLayer::onClose(CCObject*){
    Popup::onClose(nullptr);
}

void DTLevelSpecificSettingsLayer::keyBackClicked(){
    Popup::keyBackClicked();
}