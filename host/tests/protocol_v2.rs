use xcan::protocol_v2::*;

fn hex(s: &str) -> Vec<u8> {
    let (pairs, remainder) = s.as_bytes().as_chunks::<2>();
    assert!(remainder.is_empty());
    pairs
        .iter()
        .map(|b| u8::from_str_radix(std::str::from_utf8(b).unwrap(), 16).unwrap())
        .collect()
}
fn vectors() -> Vec<(bool, &'static str, Vec<u8>)> {
    include_str!("../../tests/protocol_v2_vectors.txt")
        .lines()
        .filter(|l| !l.starts_with('#'))
        .map(|l| {
            let mut words = l.split_whitespace();
            (
                words.next().unwrap() == "1",
                words.next().unwrap(),
                hex(words.next().unwrap()),
            )
        })
        .collect()
}
fn fixture(name: &str) -> Message {
    Message::decode(&vectors().into_iter().find(|v| v.1 == name).unwrap().2).unwrap()
}

#[test]
fn shared_vectors_all_chunk_sizes_and_coalescing() {
    let cases = vectors();
    assert_eq!(cases.len(), 106);
    for (valid, name, wire) in cases {
        assert_eq!(Message::decode(&wire).is_ok(), valid, "{name}");
        for chunk in 1..=wire.len() {
            let mut decoder = Decoder::default();
            let mut offset = 0;
            let mut count = 0;
            let mut failed = false;
            while offset < wire.len() {
                let end = (offset + chunk).min(wire.len());
                match decoder.feed(&wire[offset..end]) {
                    Ok((n, m)) => {
                        assert!(n > 0 && n <= end - offset);
                        offset += n;
                        count += usize::from(m.is_some());
                    }
                    Err(_) => {
                        failed = true;
                        assert!(decoder.feed(&wire).is_err());
                        break;
                    }
                }
            }
            assert_eq!(
                !failed && decoder.finish().is_ok() && count == 1,
                valid,
                "{name}, chunk={chunk}"
            );
            if !valid {
                assert!(decoder.feed(&wire).is_err());
            }
        }
        if valid {
            let m = Message::decode(&wire).unwrap();
            assert_eq!(m.encode().unwrap(), wire, "{name}");
            let joined = [wire.as_slice(), wire.as_slice()].concat();
            let mut decoder = Decoder::default();
            let (n, first) = decoder.feed(&joined).unwrap();
            assert_eq!(n, wire.len());
            assert_eq!(first, Some(m.clone()));
            let (n2, second) = decoder.feed(&joined[n..]).unwrap();
            assert_eq!(n2, wire.len());
            assert_eq!(second, Some(m));
            assert!(decoder.finish().is_ok());
        }
    }
}

#[test]
fn semantic_frames_preserve_full_fd_and_timestamps() {
    for (valid, _, wire) in vectors() {
        if !valid {
            continue;
        }
        let m = Message::decode(&wire).unwrap();
        let tx = m.kind == REQUEST && m.opcode == SEND;
        if !tx && !(m.kind == EVENT && m.opcode == FRAMES) {
            continue;
        }
        let mut pos = if tx { 0 } else { 8 };
        while pos < m.payload.len() {
            let (f, n) = CanFrame::decode(&m.payload[pos..], tx).unwrap();
            assert_eq!(f.encode(tx).unwrap(), m.payload[pos..pos + n]);
            pos += n;
        }
        assert_eq!(pos, m.payload.len());
    }
    let m = fixture("fd_dlc_15");
    let (f, _) = CanFrame::decode(&m.payload[8..], false).unwrap();
    assert_eq!(f.timestamp_us, 0x0102030405060708);
    assert_eq!(f.sequence, 0xa1b2c3d4);
    assert_eq!(f.id, 0x1fffffff);
    assert_eq!(f.data, (0..64).collect::<Vec<u8>>());
    let mut bad = f;
    bad.flags |= 2;
    assert!(bad.encode(false).is_err());
}

fn connected() -> ClientSession {
    let mut session = ClientSession::default();
    let request = session.request(HELLO, vec![]).unwrap();
    assert_eq!(request.id, 1);
    assert_eq!(request.session, 0);
    assert_eq!(session.receive(&fixture("hello_reply")).unwrap(), None);
    session
}

#[test]
fn events_do_not_consume_control_response_and_gaps_are_visible() {
    let mut session = connected();
    let request = session.request(STATUS, vec![]).unwrap();
    assert!(session.request(STOP, vec![]).is_err());
    let mut event = fixture("mixed_batch");
    event.sequence = 0;
    assert_eq!(session.receive(&event).unwrap(), Some(0));
    event.sequence = 3;
    assert_eq!(session.receive(&event).unwrap(), Some(2));
    let mut reply = fixture("status_reply");
    reply.id = request.id;
    assert_eq!(session.receive(&reply).unwrap(), None);
    assert!(session.request(STOP, vec![]).is_ok());
}

#[test]
fn stale_duplicate_mismatched_response_and_timeout_invalidate() {
    for mode in 0..5 {
        let mut session = connected();
        let req = session.request(STATUS, vec![]).unwrap();
        let mut reply = fixture("status_reply");
        reply.id = req.id;
        match mode {
            0 => reply.session += 1,
            1 => reply.id += 1,
            2 => {
                session.receive(&reply).unwrap();
            }
            3 => session.invalidate(),
            _ => {
                reply = fixture("config_ok");
                reply.id = req.id;
            }
        }
        assert!(session.receive(&reply).is_err());
        assert!(session.request(STATUS, vec![]).is_err());
    }
    let mut session = connected();
    let event = fixture("fd_dlc_15");
    session.receive(&event).unwrap();
    assert!(session.receive(&event).is_err());
}

#[test]
fn tx_acceptance_and_async_completion_are_distinct() {
    let mut session = connected();
    let req = session
        .request(SEND, fixture("tx_fd_dlc_15").payload)
        .unwrap();
    let mut accepted = fixture("tx_accepted");
    accepted.id = req.id;
    assert_eq!(session.receive(&accepted).unwrap(), None);
    assert!(session
        .request(SEND, fixture("tx_fd_dlc_15").payload)
        .is_err());
    let mut complete = fixture("tx_result");
    complete.payload[..4].copy_from_slice(&req.id.to_le_bytes());
    assert_eq!(session.receive(&complete).unwrap(), Some(0));
    assert_ne!(accepted.kind, complete.kind);
    assert!(session
        .request(SEND, fixture("tx_fd_dlc_15").payload)
        .is_ok());
}

#[test]
fn premature_or_unrelated_tx_result_invalidates_session() {
    for premature in [true, false] {
        let mut session = connected();
        let req = session
            .request(SEND, fixture("tx_fd_dlc_15").payload)
            .unwrap();
        if !premature {
            let mut accepted = fixture("tx_accepted");
            accepted.id = req.id;
            session.receive(&accepted).unwrap();
        }
        let mut event = fixture("tx_result");
        let id = if premature { req.id } else { req.id + 1 };
        event.payload[..4].copy_from_slice(&id.to_le_bytes());
        assert!(session.receive(&event).is_err());
        assert!(session.request(STATUS, vec![]).is_err());
    }
    let mut session = connected();
    let req = session
        .request(SEND, fixture("tx_fd_dlc_15").payload)
        .unwrap();
    let mut busy = fixture("error_4");
    busy.id = req.id;
    session.receive(&busy).unwrap();
    assert!(session
        .request(SEND, fixture("tx_fd_dlc_15").payload)
        .is_ok());
}

#[test]
fn scripted_configure_start_capture_status_stop_transcript() {
    let mut session = connected();
    for (op, request_fixture, response_fixture) in [
        (CONFIGURE, "config_fd", "config_ok"),
        (START, "start", "started"),
    ] {
        let req = session
            .request(op, fixture(request_fixture).payload)
            .unwrap();
        let mut reply = fixture(response_fixture);
        reply.id = req.id;
        session
            .receive(&Message::decode(&reply.encode().unwrap()).unwrap())
            .unwrap();
    }
    let status = session.request(STATUS, vec![]).unwrap();
    let mut messages = [
        fixture("fd_dlc_15"),
        fixture("state_event"),
        fixture("status_reply"),
    ];
    messages[1].sequence = 1;
    messages[2].id = status.id;
    let wire: Vec<u8> = messages.iter().flat_map(|m| m.encode().unwrap()).collect();
    let mut decoder = Decoder::default();
    let mut offset = 0;
    let mut received = 0;
    while offset < wire.len() {
        let end = (offset + 64).min(wire.len());
        let (n, message) = decoder.feed(&wire[offset..end]).unwrap();
        offset += n;
        if let Some(m) = message {
            assert_eq!(m, messages[received]);
            session.receive(&m).unwrap();
            received += 1;
        }
    }
    assert_eq!(received, 3);
    assert!(decoder.finish().is_ok());
    let stop = session.request(STOP, vec![]).unwrap();
    let mut reply = fixture("stopped");
    reply.id = stop.id;
    session.receive(&reply).unwrap();
}
