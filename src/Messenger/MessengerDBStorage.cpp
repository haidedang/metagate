#include "MessengerDBStorage.h"

#include "MessengerDBRes.h"
#include <QtSql>

#include "check.h"
#include "Log.h"

#include <iostream>

SET_LOG_NAMESPACE("MSG");

namespace messenger {


MessengerDBStorage::MessengerDBStorage(const QString &path)
    : DBStorage(path, databaseName)
{

}

int messenger::MessengerDBStorage::currentVersion() const
{
    return databaseVersion;
}

void MessengerDBStorage::addMessage(const QString &user, const QString &duser, const QString &text, const QString &decryptedText, bool isDecrypted,
                                    uint64_t timestamp, Message::Counter counter, bool isIncoming,
                                    bool canDecrypted, bool isConfirmed, const QString &hash,
                                    qint64 fee, const QString &channelSha)
{
    DbId userid = getUserId(user);
    CHECK(userid != not_found, "User not created: " + user.toStdString());
    DbId contactid = -1;
    DbId channelid = -1;
    if (channelSha.isEmpty()) {
        CHECK(!duser.isEmpty(), "No contact or channel");
        contactid = getContactIdOrCreate(duser);
    } else {
        channelid = getChannelForUserShaName(user, channelSha);
    }

    QSqlQuery query(database());
    CHECK(query.prepare(insertMsgMessages), query.lastError().text().toStdString());
    query.bindValue(":userid", userid);
    if (channelSha.isEmpty()) {
        CHECK(contactid != not_found, "Contact not created");
        query.bindValue(":contactid", contactid);
        query.bindValue(":channelid", QVariant());
    } else {
        CHECK(channelid != not_found, "Channel not found " + channelSha.toStdString());
        query.bindValue(":channelid", channelid);
        query.bindValue(":contactid", QVariant());
    }
    query.bindValue(":order", counter);
    query.bindValue(":dt", static_cast<qint64>(timestamp));
    query.bindValue(":text", text);
    query.bindValue(":decryptedText", decryptedText);
    query.bindValue(":isDecrypted", isDecrypted);
    query.bindValue(":isIncoming", isIncoming);
    query.bindValue(":canDecrypted", canDecrypted);
    query.bindValue(":isConfirmed", isConfirmed);
    query.bindValue(":hash", hash);
    query.bindValue(":fee", fee);
    CHECK(query.exec(), query.lastError().text().toStdString());
    addLastReadRecord(userid, contactid, channelid);
}

void MessengerDBStorage::addMessage(const Message &message) {
    addMessage(message.username, message.collocutor, message.dataHex, message.decryptedDataHex, message.isDecrypted,
               message.timestamp, message.counter, message.isInput,
               message.isCanDecrypted, message.isConfirmed, message.hash,
               message.fee, message.channel);
}

void MessengerDBStorage::addMessages(const std::vector<Message> &messages) {
    auto transactionGuard = beginTransaction();
    for (const Message &message: messages) {
        addMessage(message);
    }
    transactionGuard.commit();
}

DBStorage::DbId MessengerDBStorage::getUserId(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgUsersForName), query.lastError().text().toStdString());
    query.bindValue(":username", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("id").toLongLong();
    } else {
        return not_found;
    }
}

DBStorage::DbId MessengerDBStorage::getUserIdOrCreate(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgUsersForName), query.lastError().text().toStdString());
    query.bindValue(":username", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("id").toLongLong();
    } else {
        CHECK(query.prepare(insertMsgUsers), query.lastError().text().toStdString());
        query.bindValue(":username", username);
        CHECK(query.exec(), query.lastError().text().toStdString());
        return query.lastInsertId().toLongLong();
    }
}

QStringList MessengerDBStorage::getUsersList() {
    QStringList res;
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgUsersList), query.lastError().text().toStdString());
    CHECK(query.exec(), query.lastError().text().toStdString());
    while (query.next()) {
        res.push_back(query.value("username").toString());
    }
    return res;
}

DBStorage::DbId MessengerDBStorage::getContactIdOrCreate(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgContactsForName), query.lastError().text().toStdString());
    query.bindValue(":username", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("id").toLongLong();
    } else {
        CHECK(query.prepare(insertMsgContacts), query.lastError().text().toStdString());
        query.bindValue(":username", username);
        CHECK(query.exec(), query.lastError().text().toStdString());
        return query.lastInsertId().toLongLong();
    }
}

QString MessengerDBStorage::getUserPublicKey(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgUserPublicKey), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("publickey").toString();
    }
    return QString();
}

ContactInfo MessengerDBStorage::getUserInfo(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgUserInfo), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    ContactInfo result;
    if (query.next()) {
        result.pubkeyRsa = query.value("publicKeyRsa").toString();
        result.txRsaHash = query.value("txHash").toString();
        result.blockchainName = query.value("blockchainName").toString();
    }
    return result;
}

void MessengerDBStorage::setUserPublicKey(const QString &username, const QString &publickey, const QString &publicKeyRsa, const QString &txHash, const QString &blockchainName) {
    getUserIdOrCreate(username);
    QSqlQuery query(database());
    CHECK(query.prepare(updateMsgUserPublicKey), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    query.bindValue(":publickey", publickey);
    query.bindValue(":publicKeyRsa", publicKeyRsa);
    query.bindValue(":txHash", txHash);
    query.bindValue(":blockchainName", blockchainName);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

QString MessengerDBStorage::getUserSignatures(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgUserSignatures), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("signatures").toString();
    }
    return QString();
}

void MessengerDBStorage::setUserSignatures(const QString &username, const QString &signatures) {
    getUserIdOrCreate(username);
    QSqlQuery query(database());
    CHECK(query.prepare(updateMsgUserSignatures), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    query.bindValue(":signatures", signatures);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

QString MessengerDBStorage::getContactPublicKey(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgContactsPublicKey), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("publickey").toString();
    }
    return QString();
}

ContactInfo MessengerDBStorage::getContactInfo(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgContactsInfoKey), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    ContactInfo result;
    if (query.next()) {
        result.pubkeyRsa = query.value("publickey").toString();
        result.txRsaHash = query.value("txHash").toString();
        result.blockchainName = query.value("blockchainName").toString();
    }
    return result;
}

void MessengerDBStorage::setContactPublicKey(const QString &username, const QString &publickey, const QString &txHash, const QString &blockchainName) {
    getContactIdOrCreate(username);
    QSqlQuery query(database());
    CHECK(query.prepare(updateMsgContactsPublicKey), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    query.bindValue(":publickey", publickey);
    query.bindValue(":txHash", txHash);
    query.bindValue(":blockchainName", blockchainName);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

Message::Counter MessengerDBStorage::getMessageMaxCounter(const QString &user, const QString &channelSha) {
    QSqlQuery query(database());
    const QString sql = selectMsgMaxCounter
            .arg(channelSha.isEmpty() ? QStringLiteral("") : selectJoinChannel)
            .arg(channelSha.isEmpty() ? selectWhereIsNotChannel : QStringLiteral(""));
    CHECK(query.prepare(sql), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    if (!channelSha.isEmpty())
        query.bindValue(":channelSha", channelSha);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("max").toLongLong();
    }
    return -1;
}

Message::Counter MessengerDBStorage::getMessageMaxConfirmedCounter(const QString &user) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgMaxConfirmedCounter), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("max").toLongLong();
    }
    return -1;
}

std::vector<Message> MessengerDBStorage::getMessagesForUser(const QString &user, qint64 from, qint64 to) {
    std::vector<Message> res;
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgMessagesForUser), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    query.bindValue(":ob", from);
    query.bindValue(":oe", to);
    CHECK(query.exec(), query.lastError().text().toStdString());
    std::vector<DbId> tmp;
    createMessagesList(query, res, tmp, false, false, false);
    return res;
}

std::vector<Message> MessengerDBStorage::getMessagesForUserAndDest(const QString &user, const QString &channelOrContact, qint64 from, qint64 to, bool isChannel) {
    std::vector<Message> res;
    QSqlQuery query(database());
    if (isChannel) {
        CHECK(query.prepare(selectMsgMessagesForUserAndChannel), query.lastError().text().toStdString());
    } else {
        CHECK(query.prepare(selectMsgMessagesForUserAndDest), query.lastError().text().toStdString());
    }
    query.bindValue(":user", user);
    if (isChannel)
        query.bindValue(":shaName", channelOrContact);
    else
        query.bindValue(":duser", channelOrContact);
    query.bindValue(":ob", from);
    query.bindValue(":oe", to);
    CHECK(query.exec(), query.lastError().text().toStdString());
    std::vector<DbId> tmp;
    createMessagesList(query, res, tmp, false, isChannel, false);
    return res;
}

std::vector<Message> MessengerDBStorage::getMessagesForUserAndDestNum(const QString &user, const QString &channelOrContact, qint64 to, qint64 num, bool isChannel) {
    std::vector<Message> res;
    QSqlQuery query(database());
    if (isChannel) {
        CHECK(query.prepare(selectMsgMessagesForUserAndChannelNum), query.lastError().text().toStdString());
    } else {
        CHECK(query.prepare(selectMsgMessagesForUserAndDestNum), query.lastError().text().toStdString());
    }
    query.bindValue(":user", user);
    if (isChannel) {
        query.bindValue(":shaName", channelOrContact);
    } else {
        query.bindValue(":duser", channelOrContact);
    }
    query.bindValue(":oe", to);
    query.bindValue(":num", num);
    CHECK(query.exec(), query.lastError().text().toStdString());
    std::vector<DbId> tmp;
    createMessagesList(query, res, tmp, false, isChannel, true);
    return res;
}

qint64 MessengerDBStorage::getMessagesCountForUserAndDest(const QString &user, const QString &duser, qint64 from) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectMsgCountMessagesForUserAndDest), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    query.bindValue(":duser", duser);
    query.bindValue(":ob", from);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("count").toLongLong();
    }
    return 0;
}

bool MessengerDBStorage::hasMessageWithCounter(const QString &username, Message::Counter counter, const QString &channelSha) {
    QSqlQuery query(database());
    const QString sql = selectCountMessagesWithCounter
            .arg(channelSha.isEmpty() ? QStringLiteral("") : selectJoinChannel)
            .arg(channelSha.isEmpty() ? selectWhereIsNotChannel : QStringLiteral(""));
    CHECK(query.prepare(sql), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    query.bindValue(":counter", counter);
    if (!channelSha.isEmpty())
        query.bindValue(":channelSha", channelSha);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("res").toBool();
    }
    return false;
}

bool MessengerDBStorage::hasUnconfirmedMessageWithHash(const QString &username, const QString &hash) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectCountNotConfirmedMessagesWithHash), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    query.bindValue(":hash", hash);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("res").toBool();
    }
    return false;
}

MessengerDBStorage::IdCounterPair MessengerDBStorage::findFirstNotConfirmedMessageWithHash(const QString &username, const QString &hash, const QString &channelSha) {
    QSqlQuery query(database());
    const QString sql = selectFirstNotConfirmedMessageWithHash
    .arg(channelSha.isEmpty() ? QStringLiteral("") : selectJoinChannel)
    .arg(channelSha.isEmpty() ? selectWhereIsNotChannel : QStringLiteral(""));
    CHECK(query.prepare(sql), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    query.bindValue(":hash", hash);
    if (!channelSha.isEmpty())
        query.bindValue(":channelSha", channelSha);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return MessengerDBStorage::IdCounterPair(query.value("id").toLongLong(),
                                        query.value("morder").toLongLong());
    }
    return MessengerDBStorage::IdCounterPair(-1, -1);
}

MessengerDBStorage::IdCounterPair MessengerDBStorage::findFirstMessageWithHash(const QString &username, const QString &hash, const QString &channelSha) {
    QSqlQuery query(database());
    const QString sql = selectFirstMessageWithHash
    .arg(channelSha.isEmpty() ? QStringLiteral("") : selectJoinChannel)
    .arg(channelSha.isEmpty() ? selectWhereIsNotChannel : QStringLiteral(""));
    CHECK(query.prepare(sql), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    query.bindValue(":hash", hash);
    if (!channelSha.isEmpty())
        query.bindValue(":channelSha", channelSha);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return MessengerDBStorage::IdCounterPair(query.value("id").toLongLong(),
                                        query.value("morder").toLongLong());
    }
    return MessengerDBStorage::IdCounterPair(-1, -1);
}

DBStorage::DbId MessengerDBStorage::findFirstNotConfirmedMessage(const QString &username) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectFirstNotConfirmedMessage), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("id").toLongLong();
    }
    return -1;
}

void MessengerDBStorage::updateMessage(DbId id, Message::Counter newCounter, bool confirmed) {
    QSqlQuery query(database());
    CHECK(query.prepare(updateMessageQuery), query.lastError().text().toStdString());
    query.bindValue(":id", id);
    query.bindValue(":counter", newCounter);
    query.bindValue(":isConfirmed", confirmed);
    query.exec();
    //CHECK(query.exec(), query.lastError().text().toStdString());
}

Message::Counter MessengerDBStorage::getLastReadCounterForUserContact(const QString &username, const QString &channelOrContact, bool isChannel) {
    QSqlQuery query(database());
    if (isChannel) {
        CHECK(query.prepare(selectLastReadCounterForUserChannel), query.lastError().text().toStdString());
    } else {
        CHECK(query.prepare(selectLastReadCounterForUserContact), query.lastError().text().toStdString());
    }
    query.bindValue(":user", username);
    if (isChannel)
        query.bindValue(":shaName", channelOrContact);
    else
        query.bindValue(":contact", channelOrContact);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("lastcounter").toLongLong();
    }
    return -1;
}

void MessengerDBStorage::setLastReadCounterForUserContact(const QString &username, const QString &channelOrContact, Message::Counter counter, bool isChannel) {
    QSqlQuery query(database());
    if (isChannel) {
        CHECK(query.prepare(updateLastReadCounterForUserChannel), query.lastError().text().toStdString());
    } else {
        CHECK(query.prepare(updateLastReadCounterForUserContact), query.lastError().text().toStdString());
    }
    query.bindValue(":counter", counter);
    query.bindValue(":user", username);
    if (isChannel)
        query.bindValue(":shaName", channelOrContact);
    else
        query.bindValue(":contact", channelOrContact);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

std::vector<MessengerDBStorage::NameCounterPair> MessengerDBStorage::getLastReadCountersForContacts(const QString &username) {
    std::vector<MessengerDBStorage::NameCounterPair> res;
    QSqlQuery query(database());
    CHECK(query.prepare(selectLastReadCountersForContacts), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    while (query.next()) {
        NameCounterPair p(query.value("username").toString(), query.value("lastcounter").toLongLong());
        res.push_back(p);
    }
    return res;
}

std::vector<MessengerDBStorage::NameCounterPair> MessengerDBStorage::getLastReadCountersForChannels(const QString &username) {
    std::vector<MessengerDBStorage::NameCounterPair> res;
    QSqlQuery query(database());
    CHECK(query.prepare(selectLastReadCountersForChannels), query.lastError().text().toStdString());
    query.bindValue(":user", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    while (query.next()) {
        NameCounterPair p(query.value("shaName").toString(), query.value("lastcounter").toLongLong());
        res.push_back(p);
    }
    return res;
}

std::vector<messenger::ChannelInfo> messenger::MessengerDBStorage::getChannelsWithLastReadCounters(const QString &username) {
    std::vector<messenger::ChannelInfo> res;
    QSqlQuery query(database());
    CHECK(query.prepare(selectChannelsWithLastReadCounters), query.lastError().text().toStdString());
    query.bindValue(":username", username);
    CHECK(query.exec(), query.lastError().text().toStdString());
    while (query.next()) {
        ChannelInfo info;
        info.title = query.value("channel").toString();
        info.titleSha = query.value("shaName").toString();
        info.admin = query.value("adminName").toString();
        info.counter = query.value("lastcounter").toLongLong();
        info.isWriter = query.value("isWriter").toBool();
        res.push_back(info);
    }
    return res;
}

void MessengerDBStorage::addChannel(DBStorage::DbId userid, const QString &channel, const QString &shaName, bool isAdmin, const QString &adminName, bool isBanned, bool isWriter, bool isVisited) {
    QSqlQuery query(database());
    CHECK(query.prepare(insertMsgChannels), query.lastError().text().toStdString());
    query.bindValue(":userid", userid);
    query.bindValue(":channel", channel);
    query.bindValue(":shaName", shaName);
    query.bindValue(":isAdmin", isAdmin);
    query.bindValue(":adminName", adminName);
    query.bindValue(":isBanned", isBanned);
    query.bindValue(":isWriter", isWriter);
    query.bindValue(":isVisited", isVisited);
    CHECK(query.exec(), query.lastError().text().toStdString());
    DBStorage::DbId id = query.lastInsertId().toLongLong();
    addLastReadRecord(userid, -1, id);
}

void MessengerDBStorage::setChannelsNotVisited(const QString &user) {
    QSqlQuery query(database());
    CHECK(query.prepare(updateSetChannelsNotVisited), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

DBStorage::DbId MessengerDBStorage::getChannelForUserShaName(const QString &user, const QString &shaName) {
    QSqlQuery query(database());
    CHECK(query.prepare(selectChannelForUserShaName), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    query.bindValue(":shaName", shaName);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (query.next()) {
        return query.value("id").toLongLong();
    }
    return -1;
}

void MessengerDBStorage::updateChannel(DBStorage::DbId id, bool isVisited) {
    QSqlQuery query(database());
    CHECK(query.prepare(updateChannelInfo), query.lastError().text().toStdString());
    query.bindValue(":id", id);
    query.bindValue(":isVisited", isVisited);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

void MessengerDBStorage::setWriterForNotVisited(const QString &user) {
    QSqlQuery query(database());
    CHECK(query.prepare(updatetWriterForNotVisited), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

ChannelInfo MessengerDBStorage::getChannelInfoForUserShaName(const QString &user, const QString &shaName) {
    ChannelInfo info;
    QSqlQuery query(database());
    CHECK(query.prepare(selectChannelInfoForUserShaName), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    query.bindValue(":shaName", shaName);
    CHECK(query.exec(), query.lastError().text().toStdString());
    if (!query.next()) {
        return info;
    }
    info.title = query.value("channel").toString();;
    info.titleSha = query.value("shaName").toString();;
    info.admin = query.value("adminName").toString();;
    info.isWriter = query.value("isWriter").toBool();;
    return info;
}

void MessengerDBStorage::setChannelIsWriterForUserShaName(const QString &user, const QString &shaName, bool isWriter) {
    QSqlQuery query(database());
    CHECK(query.prepare(updateChannelIsWriterForUserShaName), query.lastError().text().toStdString());
    query.bindValue(":user", user);
    query.bindValue(":shaName", shaName);
    query.bindValue(":isWriter", isWriter);
    CHECK(query.exec(), query.lastError().text().toStdString());
}

void MessengerDBStorage::removeDecryptedData() {
    QSqlQuery query(database());
    CHECK(query.prepare(removeDecryptedDataQuery), query.lastError().text().toStdString());
    CHECK(query.exec(), query.lastError().text().toStdString());
}

std::pair<std::vector<MessengerDBStorage::DbId>, std::vector<Message>> MessengerDBStorage::getNotDecryptedMessage(const QString &user) {
    std::vector<Message> result;
    std::vector<DbId> ids;
    {
        std::vector<Message> messages;
        std::vector<DbId> ids1;
        QSqlQuery query(database());
        CHECK(query.prepare(selectNotDecryptedMessagesContactsQuery), query.lastError().text().toStdString());
        query.bindValue(":user", user);
        CHECK(query.exec(), query.lastError().text().toStdString());
        createMessagesList(query, messages, ids1, true, false, false);

        std::move(messages.begin(), messages.end(), std::back_inserter(result));
        std::move(ids1.begin(), ids1.end(), std::back_inserter(ids));
    }
    {
        std::vector<Message> messages;
        std::vector<DbId> ids1;
        QSqlQuery query(database());
        CHECK(query.prepare(selectNotDecryptedMessagesChannelsQuery), query.lastError().text().toStdString());
        query.bindValue(":user", user);
        CHECK(query.exec(), query.lastError().text().toStdString());
        createMessagesList(query, messages, ids1, true, true, false);

        std::move(messages.begin(), messages.end(), std::back_inserter(result));
        std::move(ids1.begin(), ids1.end(), std::back_inserter(ids));
    }

    CHECK(result.size() == ids.size(), "Incorrect result");
    return std::make_pair(ids, result);
}

void MessengerDBStorage::updateDecryptedMessage(const std::vector<std::tuple<DbId, bool, QString>> &messages) {
    auto transactionGuard = beginTransaction();
    for (const auto &messageTuple: messages) {
        QSqlQuery query(database());
        CHECK(query.prepare(updateDecryptedMessageQuery), query.lastError().text().toStdString());
        query.bindValue(":id", std::get<0>(messageTuple));
        query.bindValue(":isDecrypted", std::get<1>(messageTuple));
        query.bindValue(":decryptedText", std::get<2>(messageTuple));
        query.exec();
    }
    transactionGuard.commit();
}

void messenger::MessengerDBStorage::createDatabase() {
    createTable(QStringLiteral("users"), createMsgUsersTable);
    createTable(QStringLiteral("contacts"), createMsgContactsTable);
    createTable(QStringLiteral("channels"), createMsgChannelsTable);
    createTable(QStringLiteral("messages"), createMsgMessagesTable);
    createTable(QStringLiteral("lastreadmessage"), createMsgLastReadMessageTable);

    createIndex(createUsersSortingIndex);
    createIndex(createUsersUniqueIndex);
    createIndex(createContactsSortingIndex);
    createIndex(createContactsUniqueIndex);
    createIndex(createMsgMessageUniqueIndex1);
    createIndex(createMsgMessageUniqueIndex2);
    createIndex(createMsgMessageCounterIndex);
    createIndex(createChannelsUniqueIndex);

    createIndex(createLastReadMessageUniqueIndex1);
    createIndex(createLastReadMessageUniqueIndex2);
}

void MessengerDBStorage::createMessagesList(QSqlQuery &query, std::vector<Message> &messages, std::vector<DbId> &ids, bool isIds, bool isChannel, bool reverse) {
    while (query.next()) {
        Message msg;
        msg.username = query.value("user").toString();
        msg.isChannel = isChannel;
        if (msg.isChannel) {
            msg.channel = query.value("dest").toString();
            msg.collocutor = QString("");
        } else {
            msg.collocutor = query.value("dest").toString();
            msg.channel = QString("");
        }
        msg.isInput = query.value("isIncoming").toBool();
        msg.dataHex = query.value("text").toString();
        msg.decryptedDataHex = query.value("decryptedText").toString();
        msg.isDecrypted = query.value("isDecrypted").toBool();
        msg.counter = query.value("morder").toLongLong();
        msg.timestamp = static_cast<quint64>(query.value("dt").toLongLong());
        msg.fee = query.value("fee").toLongLong();
        msg.isCanDecrypted = query.value("canDecrypted").toBool();
        msg.isConfirmed = query.value("isConfirmed").toBool();
        msg.hash = query.value("hash").toString();
        messages.push_back(msg);

        if (isIds) {
            ids.emplace_back(query.value("id").toLongLong());
        }
    }
    if (reverse) {
        std::reverse(std::begin(messages), std::end(messages));
        std::reverse(std::begin(ids), std::end(ids));
    }
}

void MessengerDBStorage::addLastReadRecord(DBStorage::DbId userid, DBStorage::DbId contactid, DBStorage::DbId channelid) {
    QSqlQuery query(database());
    CHECK(query.prepare(insertLastReadMessageRecord), query.lastError().text().toStdString());
    query.bindValue(":userid", userid);
    if (contactid == -1) {
        query.bindValue(":contactid", QVariant());
    } else {
        query.bindValue(":contactid", contactid);
    }
    if (channelid == -1) {
        query.bindValue(":channelid", QVariant());
    } else {
        query.bindValue(":channelid", channelid);
    }
    CHECK(query.exec(), query.lastError().text().toStdString());
}

}
