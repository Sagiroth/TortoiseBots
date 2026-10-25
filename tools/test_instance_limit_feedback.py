"""Compile the production transfer-abort branch with session/packet doubles."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.test_nefarian_bone_target import function


class InstanceLimitFeedbackTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_actual_abort_reason_and_current_controller(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE', root / 'ai/playerbot/PlayerbotAIPackets.cpp'))
        text = source.read_text()
        text = text[text.index('void PlayerbotAI::HandleBotOutgoingPacket('):]
        marker = 'case SMSG_TRANSFER_ABORTED:'
        branch = function(text, marker) if marker in text else ''
        fixture = r'''
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using uint8 = uint8_t;
constexpr unsigned SMSG_TRANSFER_ABORTED=1, TRANSFER_ABORT_TOO_MANY_INSTANCES=9;
constexpr unsigned MAX_INSTANCE_PER_ACCOUNT_PER_HOUR=5;
struct Session {
 bool headless=true;
 bool IsHeadless() const { return headless; }
 bool HasNetworkTransport() const { return !headless; }
};
struct Player {
 unsigned guid; Session* session;
 Session* GetSession() { return session; }
 unsigned GetObjectGuid() { return guid; }
 char const* GetName() { return "FixtureBot"; }
};
struct WorldPacket {
 unsigned opcode=1; std::vector<uint8> bytes{9}; unsigned cursor=0;
 unsigned size() const { return bytes.size(); }
 void rpos(unsigned n) { cursor=n; }
 WorldPacket& operator>>(uint8& reason) { reason=bytes.at(cursor++); return *this; }
};
namespace TortoiseBots {
struct BotManager {
 bool managed=true;
 static BotManager& Instance() { static BotManager m; return m; }
 bool IsBot(unsigned) { return managed; }
};
}
struct Notice { unsigned recipient; std::string text; };
std::vector<Notice> notices;
struct ChatHandler {
 Player* player;
 explicit ChatHandler(Player* p):player(p) {}
 void PSendSysMessage(char const* format, char const* name, unsigned limit) {
  char text[256]; std::snprintf(text,sizeof(text),format,name,limit);
  notices.push_back({player->guid,text});
 }
};
struct AI {
 Player* bot; Player* controller;
 unsigned instanceLimitNoticeMaster=0;
 std::chrono::steady_clock::time_point instanceLimitNoticeTime{};
 unsigned otherPackets=0;
 Player* GetMaster() { return controller; }
 void Handle(WorldPacket const& packet) {
  switch(packet.opcode) {
''' + branch + r'''
   default: ++otherPackets; break;
  }
 }
};
int main() {
 Session headless, network{false};
 Player bot{1,&headless}, master{2,&network}, next{3,&network};
 AI ai{&bot,&master}; WorldPacket packet; packet.cursor=1;
 ai.Handle(packet);
 assert(notices.size()==1 && notices.back().recipient==2);
 assert(notices.back().text.find("FixtureBot can't enter: bot account hit the 5 instances/hour limit") == 0);
 assert(packet.cursor==1); // Handler reads a copy, regardless of the input cursor.
 ai.Handle(packet); assert(notices.size()==1);
 ai.controller=&next; ai.Handle(packet);
 assert(notices.size()==2 && notices.back().recipient==3);
 ai.instanceLimitNoticeTime -= std::chrono::seconds(30);
 ai.Handle(packet); assert(notices.size()==3);
 auto reset=[&] { ai.instanceLimitNoticeMaster=0; };
 reset(); packet.bytes[0]=8; ai.Handle(packet); assert(notices.size()==3);
 packet.bytes.clear(); ai.Handle(packet); assert(notices.size()==3);
 packet.bytes={9}; ai.controller=nullptr; ai.Handle(packet); assert(notices.size()==3);
 ai.controller=&bot; ai.Handle(packet); assert(notices.size()==3);
 ai.controller=&master; master.session=nullptr; ai.Handle(packet); assert(notices.size()==3);
 master.session=&network; bot.session=&network; ai.Handle(packet); assert(notices.size()==3);
 bot.session=nullptr; ai.Handle(packet); assert(notices.size()==3);
 bot.session=&headless; TortoiseBots::BotManager::Instance().managed=false;
 ai.Handle(packet); assert(notices.size()==3);
 TortoiseBots::BotManager::Instance().managed=true;
 ai.bot=nullptr; ai.Handle(packet); assert(notices.size()==3); ai.bot=&bot;
 packet.opcode=99; ai.Handle(packet); assert(ai.otherPackets>=1);
 packet.opcode=1; ai.Handle(packet);
 assert(notices.size()==4 && notices.back().recipient==2); // Invalid attempts did not suppress valid notice.
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp) / 'test.cpp'
            exe = pathlib.Path(tmp) / 'test'
            cpp.write_text(fixture)
            subprocess.run(['g++', '-std=c++17', '-O0', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_existing_headless_packet_hook_is_used(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        adapter = (root / 'host/BotPacketAdapter.cpp').read_text()
        body = function(adapter, 'bool BotPacketAdapter::CanPacketSend(')
        self.assertIn('session->IsHeadless()', body)
        self.assertIn('ai->HandleBotOutgoingPacket(packet)', body)


if __name__ == '__main__':
    unittest.main()
