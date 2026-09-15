//! Transport-independent v2 codec. The USB CLI still speaks bringup v1.
pub const HEADER: usize = 24;
pub const MAX_MESSAGE: usize = 544;
pub const REQUEST: u8 = 1;
pub const RESPONSE: u8 = 2;
pub const EVENT: u8 = 3;
pub const HELLO: u16 = 1;
pub const STATUS: u16 = 2;
pub const CONFIGURE: u16 = 0x10;
pub const START: u16 = 0x11;
pub const STOP: u16 = 0x12;
pub const SEND: u16 = 0x13;
pub const FW_BEGIN: u16 = 0x20;
pub const FW_STATUS: u16 = 0x21;
pub const FW_WRITE: u16 = 0x22;
pub const FW_FINISH: u16 = 0x23;
pub const FW_ABORT: u16 = 0x24;
pub const FW_REBOOT: u16 = 0x25;
pub const FRAMES: u16 = 0x8001;
pub const STATE: u16 = 0x8002;
pub const TX_RESULT: u16 = 0x8003;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Invalid;
type Result<T> = std::result::Result<T, Invalid>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Message {
    pub kind: u8,
    pub opcode: u16,
    pub status: u16,
    pub id: u32,
    pub session: u32,
    pub sequence: u32,
    pub payload: Vec<u8>,
}

fn u16le(p: &[u8]) -> u16 {
    u16::from_le_bytes(p[..2].try_into().unwrap())
}
fn u32le(p: &[u8]) -> u32 {
    u32::from_le_bytes(p[..4].try_into().unwrap())
}
fn check(ok: bool) -> Result<()> {
    if ok {
        Ok(())
    } else {
        Err(Invalid)
    }
}

fn envelope(m: &Message, length: usize) -> Result<()> {
    check(length <= MAX_MESSAGE - HEADER && (REQUEST..=EVENT).contains(&m.kind))?;
    if m.kind == EVENT {
        return check(m.opcode >= 0x8000 && m.id == 0 && m.session != 0 && m.status == 0);
    }
    check(m.opcode != 0 && m.opcode < 0x8000 && m.id != 0 && m.sequence == 0)?;
    check((m.kind != REQUEST || m.status == 0) && m.status <= 6 && (m.status == 0 || length == 0))?;
    if m.opcode == HELLO && (m.kind == REQUEST || m.status != 0) {
        check(m.session == 0)
    } else {
        check(m.session != 0)
    }
}

fn frame_size(p: &[u8], tx: bool) -> Result<usize> {
    const LENGTHS: [usize; 16] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64];
    check(p.len() >= 24)?;
    let flags = u16le(&p[2..]);
    let id = u32le(&p[4..]);
    let dlc = p[20] as usize;
    let length = p[21] as usize;
    let rtr = flags & 2 != 0;
    let fd = flags & 4 != 0;
    check(p[0] == 0 && p[1] == 0 && flags & !31 == 0 && p[22..24] == [0, 0])?;
    check(id <= if flags & 1 != 0 { 0x1fff_ffff } else { 0x7ff })?;
    check(dlc <= 15 && length <= 64 && p.len() >= 24 + length)?;
    check((fd || (dlc <= 8 && flags & 24 == 0)) && !(fd && rtr))?;
    check(length == if rtr { 0 } else { LENGTHS[dlc] })?;
    check(!tx || (flags & 16 == 0 && p[8..20].iter().all(|v| *v == 0)))?;
    Ok(24 + length)
}

fn state(p: &[u8]) -> bool {
    p.len() == 40 && p[0] <= 4 && p[1] <= 1 && (p[2] <= 1 || p[2] == 255) && p[3] == 0
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CanFrame {
    pub flags: u16,
    pub id: u32,
    pub timestamp_us: u64,
    pub sequence: u32,
    pub dlc: u8,
    pub data: Vec<u8>,
}
impl CanFrame {
    pub fn encode(&self, tx: bool) -> Result<Vec<u8>> {
        check(self.data.len() <= 64)?;
        let mut p = Vec::with_capacity(24 + self.data.len());
        p.extend_from_slice(&[0, 0]);
        p.extend_from_slice(&self.flags.to_le_bytes());
        p.extend_from_slice(&self.id.to_le_bytes());
        p.extend_from_slice(&self.timestamp_us.to_le_bytes());
        p.extend_from_slice(&self.sequence.to_le_bytes());
        p.extend_from_slice(&[self.dlc, self.data.len() as u8, 0, 0]);
        p.extend_from_slice(&self.data);
        frame_size(&p, tx)?;
        Ok(p)
    }
    pub fn decode(p: &[u8], tx: bool) -> Result<(Self, usize)> {
        let size = frame_size(p, tx)?;
        Ok((
            Self {
                flags: u16le(&p[2..]),
                id: u32le(&p[4..]),
                timestamp_us: u64::from_le_bytes(p[8..16].try_into().unwrap()),
                sequence: u32le(&p[16..]),
                dlc: p[20],
                data: p[24..size].to_vec(),
            },
            size,
        ))
    }
}

impl Message {
    pub fn validate(&self) -> Result<()> {
        envelope(self, self.payload.len())?;
        if self.status != 0 {
            return Ok(());
        }
        let p = self.payload.as_slice();
        let n = p.len();
        let valid = match (self.kind, self.opcode) {
            (
                REQUEST,
                HELLO | STATUS | START | STOP | FW_STATUS | FW_FINISH | FW_ABORT | FW_REBOOT,
            ) => n == 0,
            (REQUEST, CONFIGURE) => {
                n == 12
                    && u32le(p) != 0
                    && p[8] <= 1
                    && p[9] <= 1
                    && p[10..12] == [0, 0]
                    && ((p[9] != 0) == (u32le(&p[4..]) != 0))
            }
            (REQUEST, SEND) => frame_size(p, true)? == n,
            (REQUEST, FW_BEGIN) => n == 36 && (520..=0xc000).contains(&u32le(p)),
            (REQUEST, FW_WRITE) => {
                (9..=520).contains(&n)
                    && u16le(&p[4..]) as usize == n - 8
                    && u16le(&p[4..]) <= 512
                    && p[6..8] == [0, 0]
                    && u32le(p) & 7 == 0
            }
            (RESPONSE, HELLO) => {
                n == 16
                    && u32le(p) & !31 == 0
                    && u16le(&p[4..]) as usize == MAX_MESSAGE
                    && p[6] == 1
                    && p[7] <= 2
                    && u32le(&p[8..]) == 1_000_000
                    && p[12..16] == [0; 4]
            }
            (RESPONSE, STATUS) | (EVENT, STATE) => state(p),
            (RESPONSE, START) => n == 4 && u32le(p) != 0,
            (RESPONSE, CONFIGURE | STOP | SEND) => n == 0,
            (RESPONSE, FW_STATUS) => n == 16 && p[0] <= 5 && p[1] <= 6 && p[2..4] == [0, 0],
            (RESPONSE, FW_BEGIN | FW_WRITE | FW_FINISH | FW_ABORT | FW_REBOOT) => n == 0,
            (EVENT, FRAMES) => {
                check(n >= 8 && u32le(p) != 0 && u16le(&p[4..]) != 0 && p[6..8] == [0, 0])?;
                let mut pos = 8;
                for _ in 0..u16le(&p[4..]) {
                    pos += frame_size(&p[pos..], false)?;
                }
                pos == n
            }
            (EVENT, TX_RESULT) => {
                n == 16 && u32le(p) != 0 && u16le(&p[4..]) <= 3 && p[6..8] == [0, 0]
            }
            _ => true, // Unknown bounded operations are dispatched/ignored explicitly.
        };
        check(valid)
    }

    pub fn encode(&self) -> Result<Vec<u8>> {
        self.validate()?;
        let mut out = Vec::with_capacity(HEADER + self.payload.len());
        out.extend_from_slice(b"XCAN");
        out.push(2);
        out.push(self.kind);
        out.extend_from_slice(&self.opcode.to_le_bytes());
        out.extend_from_slice(&(self.payload.len() as u16).to_le_bytes());
        out.extend_from_slice(&self.status.to_le_bytes());
        for v in [self.id, self.session, self.sequence] {
            out.extend_from_slice(&v.to_le_bytes());
        }
        out.extend_from_slice(&self.payload);
        Ok(out)
    }

    fn header(p: &[u8]) -> Result<(Self, usize)> {
        check(p.len() >= HEADER && &p[..4] == b"XCAN" && p[4] == 2)?;
        let m = Self {
            kind: p[5],
            opcode: u16le(&p[6..]),
            status: u16le(&p[10..]),
            id: u32le(&p[12..]),
            session: u32le(&p[16..]),
            sequence: u32le(&p[20..]),
            payload: Vec::new(),
        };
        let length = u16le(&p[8..]) as usize;
        envelope(&m, length)?;
        Ok((m, length))
    }

    pub fn decode(p: &[u8]) -> Result<Self> {
        let (mut m, length) = Self::header(p)?;
        check(p.len() == HEADER + length)?;
        m.payload.extend_from_slice(&p[HEADER..]);
        m.validate()?;
        Ok(m)
    }
}

#[derive(Default)]
pub struct Decoder {
    bytes: Vec<u8>,
    failed: bool,
}
impl Decoder {
    /// Returns consumed bytes and at most one message. Failure is latched.
    pub fn feed(&mut self, input: &[u8]) -> Result<(usize, Option<Message>)> {
        if self.failed {
            return Err(Invalid);
        }
        let result = self.feed_inner(input);
        if result.is_err() {
            self.failed = true;
        }
        result
    }
    fn feed_inner(&mut self, input: &[u8]) -> Result<(usize, Option<Message>)> {
        let mut used = 0;
        while used < input.len() {
            let target = if self.bytes.len() < HEADER {
                HEADER
            } else {
                HEADER + u16le(&self.bytes[8..]) as usize
            };
            let n = (target - self.bytes.len()).min(input.len() - used);
            self.bytes.extend_from_slice(&input[used..used + n]);
            used += n;
            if self.bytes.len() < HEADER {
                continue;
            }
            let (_, length) = Message::header(&self.bytes)?;
            if self.bytes.len() == HEADER + length {
                let message = Message::decode(&self.bytes)?;
                self.bytes.clear();
                return Ok((used, Some(message)));
            }
        }
        Ok((used, None))
    }
    /// A partial record at end-of-stream is a truncated message, not success.
    pub fn finish(&mut self) -> Result<()> {
        if !self.bytes.is_empty() {
            self.failed = true;
        }
        check(!self.failed)
    }
}

/// Correlation only: no I/O, retry, device state machine or physical CAN action.
#[derive(Default)]
pub struct ClientSession {
    session: u32,
    next_id: u32,
    next_event: u32,
    pending: Option<(u32, u16)>,
    pending_tx: Option<u32>,
    failed: bool,
}
impl ClientSession {
    pub fn request(&mut self, opcode: u16, payload: Vec<u8>) -> Result<Message> {
        check(opcode != SEND || self.pending_tx.is_none())?;
        check(!self.failed && self.pending.is_none() && self.next_id != u32::MAX)?;
        check((self.session == 0) == (opcode == HELLO))?;
        let m = Message {
            kind: REQUEST,
            opcode,
            status: 0,
            id: self.next_id + 1,
            session: self.session,
            sequence: 0,
            payload,
        };
        m.validate()?;
        self.next_id = m.id;
        self.pending = Some((m.id, opcode));
        if opcode == SEND {
            self.pending_tx = Some(m.id);
        }
        Ok(m)
    }
    /// None = correlated response; Some(n) = event, with n missing event records.
    pub fn receive(&mut self, m: &Message) -> Result<Option<u32>> {
        let result = self.receive_inner(m);
        if result.is_err() {
            self.invalidate();
        }
        result
    }
    fn receive_inner(&mut self, m: &Message) -> Result<Option<u32>> {
        check(!self.failed)?;
        m.validate()?;
        if self.session == 0 {
            check(m.kind == RESPONSE && m.opcode == HELLO && self.pending == Some((m.id, HELLO)))?;
            self.pending = None;
            if m.status != 0 {
                self.invalidate();
            } else {
                self.session = m.session;
            }
            return Ok(None);
        }
        check(m.session == self.session)?;
        if m.kind == EVENT {
            if m.opcode == TX_RESULT {
                let id = u32le(&m.payload);
                check(self.pending_tx == Some(id) && self.pending != Some((id, SEND)))?;
                self.pending_tx = None;
            }
            let gap = m.sequence.wrapping_sub(self.next_event);
            check(gap < (1 << 31))?;
            self.next_event = m.sequence.wrapping_add(1);
            return Ok(Some(gap));
        }
        check(m.kind == RESPONSE && self.pending == Some((m.id, m.opcode)))?;
        self.pending = None;
        if m.opcode == SEND && m.status != 0 {
            self.pending_tx = None;
        }
        if m.status == 5 {
            self.invalidate();
        }
        Ok(None)
    }
    /// Call on timeout, disconnect or transport error. Reconfigure USB before a new instance.
    pub fn invalidate(&mut self) {
        self.failed = true;
        self.pending = None;
        self.pending_tx = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sequence_wrap_and_request_id_exhaustion() {
        let mut client = ClientSession {
            session: 1,
            next_event: u32::MAX,
            ..Default::default()
        };
        let mut event = Message {
            kind: EVENT,
            opcode: 0x8888,
            status: 0,
            id: 0,
            session: 1,
            sequence: u32::MAX,
            payload: vec![],
        };
        assert_eq!(client.receive(&event).unwrap(), Some(0));
        event.sequence = 0;
        assert_eq!(client.receive(&event).unwrap(), Some(0));
        client.next_id = u32::MAX;
        assert!(client.request(STATUS, vec![]).is_err());
        assert!(client.pending.is_none());
    }

    #[test]
    fn oversized_header_is_rejected_before_body_allocation() {
        let mut wire = [0u8; HEADER];
        wire[..4].copy_from_slice(b"XCAN");
        wire[4] = 2;
        wire[5] = EVENT;
        wire[6..8].copy_from_slice(&0x8888u16.to_le_bytes());
        wire[8..10].copy_from_slice(&u16::MAX.to_le_bytes());
        wire[16] = 1;
        let mut decoder = Decoder::default();
        assert!(decoder.feed(&wire).is_err());
        assert!(decoder.bytes.len() <= HEADER);
        assert!(decoder.feed(&[0; 512]).is_err());
    }
}
