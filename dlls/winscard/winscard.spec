@ stub ClassInstall32
@ stub SCardAccessNewReaderEvent
@ stub SCardReleaseAllEvents
@ stub SCardReleaseNewReaderEvent
@ stub SCardAccessStartedEvent
@ stdcall SCardAddReaderToGroupA(long str str)
@ stdcall SCardAddReaderToGroupW(long wstr wstr)
@ stub SCardBeginTransaction
@ stub SCardCancel
@ stub SCardConnectA
@ stub SCardConnectW
@ stub SCardControl
@ stub SCardDisconnect
@ stub SCardEndTransaction
@ stdcall SCardEstablishContext(long ptr ptr ptr)
@ stub SCardForgetCardTypeA
@ stub SCardForgetCardTypeW
@ stub SCardForgetReaderA
@ stub SCardForgetReaderGroupA
@ stub SCardForgetReaderGroupW
@ stub SCardForgetReaderW
@ stub SCardFreeMemory
@ stub SCardGetAttrib
@ stub SCardGetCardTypeProviderNameA
@ stub SCardGetCardTypeProviderNameW
@ stub SCardGetProviderIdA
@ stub SCardGetProviderIdW
@ stub SCardGetStatusChangeA
@ stub SCardGetStatusChangeW
@ stub SCardIntroduceCardTypeA
@ stub SCardIntroduceCardTypeW
@ stub SCardIntroduceReaderA
@ stub SCardIntroduceReaderGroupA
@ stub SCardIntroduceReaderGroupW
@ stub SCardIntroduceReaderW
@ stub SCardIsValidContext
@ stub SCardListCardsA
@ stub SCardListCardsW
@ stub SCardListInterfacesA
@ stub SCardListInterfacesW
@ stub SCardListReaderGroupsA
@ stub SCardListReaderGroupsW
@ stub SCardListReadersA
@ stub SCardListReadersW
@ stub SCardLocateCardsA
@ stub SCardLocateCardsByATRA
@ stub SCardLocateCardsByATRW
@ stub SCardLocateCardsW
@ stub SCardReconnect
@ stub SCardReleaseContext
@ stub SCardReleaseStartedEvent
@ stub SCardRemoveReaderFromGroupA
@ stub SCardRemoveReaderFromGroupW
@ stub SCardSetAttrib
@ stub SCardSetCardTypeProviderNameA
@ stub SCardSetCardTypeProviderNameW
@ stub SCardState
@ stub SCardStatusA
@ stub SCardStatusW
@ stub SCardTransmit
@ extern g_rgSCardRawPci
@ extern g_rgSCardT0Pci
@ extern g_rgSCardT1Pci
