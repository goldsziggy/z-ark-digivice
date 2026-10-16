// Included inside the actual runtime/SaveStore harness, with real Device and
// PN532 Session linked. ONLY UART/SDK/radio/storage hardware is doubled.
void nfcPump(Fixture& f,unsigned ms,unsigned step=20){
    for(unsigned elapsed=0;elapsed<ms;elapsed+=step){
        const auto io=nfcmock::reads+nfcmock::writes;
        const auto writes=nfcmock::writes,drains=nfcmock::flushes+nfcmock::closes;
        f.tick(step);
        // The protocol performs one operation; explicit flush/close may also
        // drain up to four 64-byte chunks for truthful discarded-byte tracing.
        const auto drainBudget=4*(nfcmock::flushes+nfcmock::closes-drains);
        require(nfcmock::writes-writes<=1&&nfcmock::reads+nfcmock::writes-io<=1+drainBudget,
                "one protocol operation plus bounded flush/close drains per owner tick");
    }
}
void nfcStop(Fixture& f){f.send("nfc stop");nfcPump(f,1600);require(!nfcmock::installed&&nfcmock::opens==nfcmock::closes,"UART released after bounded cleanup");}
void nfcAbsent(Fixture& f){nfcmock::uid.clear();nfcPump(f,1000);}
void nfcGameplayRuntime(){
    using namespace digivice;
    { // No automatic scans before an eligible encounter. Held at entry is disarmed.
        fake::reset();Fixture f;nfcPump(f,1000);require(!nfcmock::calls,"home performs no UART calls");
        nfcmock::uid={0xc0,0xde,0,1};f.dispatch("walk 100");nfcPump(f,1000);
        require(!f.state.cardUsed,"held card at battle entry requires removal");
        require(nfcmock::samCommands==1&&!nfcmock::firmwareBeforeSam,"cold UART initializes SAM before firmware");
        nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,1};const auto writes=f.backend.writes;
        nfcPump(f,600);require(f.state.digiModifyReady==1&&f.state.cardUsed&&f.backend.writes==writes+1,"UART frames reach actual saved critical handler exactly once");
        nfcPump(f,1000);require(f.backend.writes==writes+1,"held UART card never duplicates");
        require(nfcmock::samCommands==1,"clean repeated scans reuse SAM setup");
        State recovered;storage::SaveStore reboot(f.backend);require(reboot.restore(recovered)==storage::BootStatus::Loaded&&sameSavedState(f.state,recovered),"UART effect survives actual SaveStore reload");
        f.dispatch("magic");require(!f.state.digiModifyReady,"gameplay commands not blocked by background scan");
        nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,400);require(!f.state.digiModifyReady,"rearm cannot bypass one-card fight allowance");
        f.dispatch("retreat");f.dispatch("walk 100");nfcPump(f,700);require(!f.state.cardUsed,"held card cannot carry into next battle");
        nfcStop(f);
    }
    { // Unknown present then UID swap is not absence, even same mapped effect.
        fake::reset();Fixture f;f.dispatch("walk 100");nfcAbsent(f);
        nfcmock::uid={9,8,7,6};nfcPump(f,500);require(!f.state.cardUsed,"unknown UID rejected by gameplay handler");
        nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,600);require(!f.state.cardUsed,"unknown-to-known swap cannot rearm");
        nfcAbsent(f);
        // Deliver one first UID reading, then a different UID for the same card ID.
        auto beforeScans=nfcmock::scans;nfcmock::uid={0xc0,0xde,0,1};
        while(nfcmock::scans==beforeScans)nfcPump(f,20);
        nfcmock::uid={0xc0,0xde,0,6};nfcPump(f,700);require(!f.state.cardUsed,"same-effect different-UID swap disarms stable presence");
        nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,6};nfcPump(f,500);require(f.state.digiModifyReady==1,"removal permits explicitly mapped second UID");nfcStop(f);
    }
    { // A complete battle transition between polls cannot reuse an in-flight UID.
        fake::reset();Fixture f;f.dispatch("walk 100");nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,1};
        const auto scans=nfcmock::scans;while(nfcmock::scans==scans)nfcPump(f,20);
        f.dispatch("retreat");f.dispatch("walk 100");nfcPump(f,2400);
        require(!f.state.cardUsed,"in-flight observation is bound to the encounter that requested it");nfcStop(f);
    }
    { // Core capture-awaiting rejection remains effective with actual UART input.
        fake::reset();Fixture f;require(apply(f.state,Action::Mode,1)==Error::None,"auto fixture");f.dispatch("walk 100");
        f.state.wildHp=f.state.wildMaxHp/2;f.state.wildTurn=1;f.state.autoCapture=AutoCapture::Awaiting;
        require(isValid(f.state)&&f.saves.checkpoint(f.state),"valid capture-awaiting fixture");
        nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,600);require(!f.state.cardUsed,"capture-awaiting scan cannot consume card");
        f.state.autoCapture=AutoCapture::None;require(isValid(f.state),"capture pause dismissed");nfcPump(f,600);require(!f.state.cardUsed,"held capture scan cannot activate after pause");nfcStop(f);
    }
    const std::vector<std::vector<std::uint8_t>> uids{{0xc0,0xde,0,1},{0xc0,0xde,0,2},{4,0xc0,0xde,0,0,0,3},{4,0xc0,0xde,0,0,0,0,0,0,4},{0xc0,0xde,0,5}};
    for(unsigned effect=0;effect<5;++effect)for(bool automatic:{false,true}){
        fake::reset();Fixture f;require(apply(f.state,Action::Mode,automatic?1:0)==Error::None,"mode fixture");f.dispatch("walk 100");
        if(effect==3){--f.state.hp;f.state.collection[0].hp=f.state.hp;}
        if(effect==4){f.state.wildFormId=f.state.collection[0].formId;f.state.wildSpecies=f.state.collection[0].species;f.state.wildLevel=f.state.level;f.state.wildMaxHp=f.state.wildHp=combat::formProfile(f.state.wildFormId,f.state.wildLevel).stats.maxHp;}
        require(f.saves.checkpoint(f.state),"effect fixture saved");nfcAbsent(f);auto before=f.state;const auto writes=f.backend.writes;nfcmock::uid=uids[effect];nfcPump(f,500);
        const auto pending=effect==3?0:effect==4?4+unsigned(combat::counterElement(before.wildFormId)):effect+1;
        require(f.state.cardUsed&&f.state.digiModifyReady==pending&&f.backend.writes==writes+1,"all five UID mappings enter the shared handler in manual/auto");
        require(effect!=3||f.state.hp==before.hp+1,"recovery actually updates member HP");nfcStop(f);
    }
    for(bool automatic:{false,true}) { // Three actual dungeon tier transitions.
        fake::reset();Fixture f;f.state.dungeonKeys=1;require(apply(f.state,Action::Mode,automatic?1:0)==Error::None,"dungeon mode fixture");
        require(apply(f.state,Action::StartExpedition,0)==Error::None&&f.saves.checkpoint(f.state),"start actual three-fight dungeon");
        for(unsigned tier=1;tier<=3;++tier){
            require(f.state.expeditionTier==tier&&f.state.phase==Phase::Encounter,"dungeon tier is active");
            if(tier>1){nfcPump(f,2200);require(!f.state.cardUsed,"held previous-tier card cannot grant new allowance");}
            nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,600);
            require(f.state.cardUsed&&f.state.digiModifyReady==1,"UART critical granted once in each dungeon tier");
            // Controlled low-HP foe fixture; resolution still uses actual core/save path.
            f.state.wildHp=1;require(f.saves.checkpoint(f.state),"low-HP dungeon fixture checkpoint");
            f.dispatch(automatic?"auto-step":"magic");require(!f.state.digiModifyReady,"tier victory clears charge");
            if(tier<3){
                require(f.state.phase==Phase::Expedition,"tier victory enters next-tier phase");
                require(apply(f.state,Action::NextExpedition,0)==Error::None&&f.saves.checkpoint(f.state),"next actual dungeon tier");
            } else require(f.state.phase==Phase::Home&&f.state.dungeonClears==1,"three-fight dungeon completed");
        }
        nfcStop(f);
    }
    for(unsigned failure=0;failure<3;++failure){
        fake::reset();Fixture f;f.dispatch("walk 100");nfcAbsent(f);const auto before=f.state;const auto writes=f.backend.writes;
        f.backend.failBefore=failure==0;f.backend.failAfter=failure==1;f.backend.failReadback=failure==2;
        nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,2200);
        require(sameSavedState(before,f.state)&&f.backend.writes==writes+1&&!f.saves.writable(),"failed/uncertain/readback save never publishes or retries UART card");nfcStop(f);
    }
    for(unsigned fault=0;fault<5;++fault){
        fake::reset();Fixture f;f.dispatch("walk 100");nfcAbsent(f);
        nfcmock::silent=fault==0;nfcmock::corrupt=fault==1;nfcmock::failOff=fault==2;nfcmock::wrongResponse=fault==3;
        if(fault==4)nfcmock::eventError=UART_FRAME_ERR;
        nfcmock::uid={0xc0,0xde,0,1};const auto writes=f.backend.writes;nfcPump(f,4000);
        require(!f.state.cardUsed&&f.backend.writes==writes,"timeout/corrupt/off failure/wrong command/UART error cannot grant an effect");
        require(nfcmock::opens<=5,"fault retry has bounded one-second backoff");
        nfcmock::silent=nfcmock::corrupt=nfcmock::failOff=nfcmock::wrongResponse=false;nfcPump(f,2200);require(!f.state.cardUsed,"recovery with held card requires real removal");
        nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,500);require(f.state.cardUsed,"fresh removal/presentation recovers after transport fault");nfcStop(f);
    }
    { // Stalled main owner discards a previously in-flight observation.
        fake::reset();Fixture f;f.dispatch("walk 100");nfcAbsent(f);nfcmock::uid={0xc0,0xde,0,1};
        f.tick(400);nfcPump(f,2200);require(!f.state.cardUsed,"stale poll cannot replay a card");
        fake::now-=100;nfcPump(f,1500);require(!f.state.cardUsed,"clock reversal cannot rearm");nfcStop(f);
    }
    { // Fresh but slow scans must not be confused with an owner stall or cached samples.
        fake::reset();Fixture f;nfcmock::responseDelayMs=300;f.dispatch("walk 100");nfcPump(f,4000);
        nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,2000);
        require(f.state.digiModifyReady==1,"two fresh slow UID scans dispatch through transaction-aware debounce");nfcStop(f);
    }
    { // Cold initialization plus genuinely slow UART replies use the new budget.
        fake::reset();Fixture f;nfcmock::responseDelayMs=900;f.dispatch("walk 100");nfcPump(f,7600);
        nfcmock::uid={0xc0,0xde,0,1};nfcPump(f,5000);
        require(nfcmock::samCommands==1&&!nfcmock::firmwareBeforeSam&&f.state.digiModifyReady==1,
                "900ms cold-reader replies reach saved gameplay without stale sampling");nfcStop(f);
    }
    { // One-byte transport fragmentation exercises the actual parser/adapter.
        fake::reset();Fixture f;nfcmock::readChunk=nfcmock::writeChunk=1;f.dispatch("walk 100");nfcPump(f,1100,1);
        nfcmock::uid={4,0xc0,0xde,0,0,0,3};nfcPump(f,700,1);require(f.state.digiModifyReady==3,"fragmented UART ACK/UID frames dispatch once");nfcStop(f);
    }
    { // Explicit diagnostics remain read-only and exclusive, even with mapped UID.
        fake::reset();Fixture f;nfcmock::uid={0xc0,0xde,0,1};f.send("nfc scan");nfcPump(f,800);
        require(!nfcmock::installed&&!f.state.cardUsed,"diagnostic UID does not grant gameplay reward");
        f.dispatch("walk 100");nfcAbsent(f);f.send("nfc stop");nfcPump(f,1200);const auto calls=nfcmock::calls;nfcPump(f,1200);
        require(nfcmock::calls==calls,"stop keeps automatic polling stopped");
        nfcmock::uid={0xc0,0xde,0,1};f.send("nfc cards on");nfcPump(f,900);require(!f.state.cardUsed,"resume while held requires removal");nfcStop(f);
    }
    { // Power cleanup is a barrier, and cards stop outside local encounters.
        fake::reset();Fixture f;f.dispatch("walk 100");nfcAbsent(f);f.dispatch("retreat");nfcPump(f,1500);
        const auto calls=nfcmock::calls;nfcPump(f,600);require(nfcmock::calls==calls&&!nfcmock::installed,"ending battle closes UART and stops scans");
        f.dispatch("walk 100");nfcAbsent(f);nfcmock::silent=true;f.press(3200);f.release();nfcPump(f,2200);
        require(!nfcmock::installed&&nfcmock::opens==nfcmock::closes,"shutdown drains/cancels UART before quiescence");
        require(!f.state.cardUsed,"power transition grants nothing");
    }
    std::printf("PASS %u UART-to-gameplay checks: actual UART Device, PN532 parser/session, HandheldRuntime::poll/observeCard, core and SaveStore; hardware I/O doubled only.\n",checks);
}
