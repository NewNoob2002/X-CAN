use rusb::{DeviceHandle, GlobalContext};
use std::{
    error::Error,
    time::{Duration, Instant},
};
type Result<T> = std::result::Result<T, Box<dyn Error>>;
const VID: u16 = 0xc0ca;
const PID: u16 = 0x0313;
const TIMEOUT: Duration = Duration::from_secs(2);

fn request(op: u8, id: u32, data: &[u8]) -> Result<[u8; 64]> {
    if data.len() > 52 {
        return Err("payload exceeds 52 bytes".into());
    }
    let mut frame = [0; 64];
    frame[..4].copy_from_slice(b"XCAN");
    frame[4] = 1;
    frame[5] = op;
    frame[7] = data.len() as u8;
    frame[8..12].copy_from_slice(&id.to_le_bytes());
    frame[12..12 + data.len()].copy_from_slice(data);
    Ok(frame)
}
fn validate(frame: &[u8; 64], op: u8, id: u32) -> Result<&[u8]> {
    if &frame[..4] != b"XCAN"
        || frame[4] != 1
        || frame[5] != op
        || frame[8..12] != id.to_le_bytes()
        || frame[7] > 52
    {
        return Err("invalid USB response header/transaction".into());
    }
    if frame[6] != 0 {
        return Err(format!("device status {}", frame[6]).into());
    }
    Ok(&frame[12..12 + frame[7] as usize])
}

struct Link {
    handle: DeviceHandle<GlobalContext>,
    interface: u8,
    ep_in: u8,
    ep_out: u8,
}
impl Link {
    fn exchange(&self, frame: &[u8; 64], split: usize) -> Result<[u8; 64]> {
        if !(1..=64).contains(&split) {
            return Err("invalid split size".into());
        }
        let deadline = Instant::now() + TIMEOUT;
        for part in frame.chunks(split) {
            let timeout = deadline
                .checked_duration_since(Instant::now())
                .ok_or("USB deadline")?;
            if self.handle.write_bulk(self.ep_out, part, timeout)? != part.len() {
                return Err("short USB write; transaction not retried".into());
            }
        }
        let mut response = [0; 64];
        let mut used = 0;
        while used < response.len() {
            let timeout = deadline
                .checked_duration_since(Instant::now())
                .ok_or("USB deadline")?;
            let n = self
                .handle
                .read_bulk(self.ep_in, &mut response[used..], timeout)?;
            if n == 0 {
                return Err("zero-length USB response".into());
            }
            used += n;
        }
        Ok(response)
    }
    fn close(self) -> Result<()> {
        self.handle.release_interface(self.interface)?;
        Ok(())
    }
}

fn open(serial: &str) -> Result<Link> {
    if serial.is_empty() {
        return Err("serial must be non-empty".into());
    }
    let mut found = None;
    for device in rusb::devices()?.iter() {
        let desc = device.device_descriptor()?;
        if desc.vendor_id() != VID || desc.product_id() != PID {
            continue;
        }
        let handle = device.open()?;
        if handle.read_serial_number_string_ascii(&desc)? != serial {
            continue;
        }
        if found.is_some() {
            return Err("duplicate USB serial; select a unique device".into());
        }
        let config = device.active_config_descriptor()?;
        let mut endpoints = None;
        for iface in config.interfaces() {
            for alt in iface.descriptors() {
                if alt.class_code() != 0xff || alt.setting_number() != 0 {
                    continue;
                }
                let mut ep_in = None;
                let mut ep_out = None;
                for ep in alt.endpoint_descriptors() {
                    if ep.transfer_type() != rusb::TransferType::Bulk {
                        continue;
                    }
                    match ep.direction() {
                        rusb::Direction::In => ep_in = Some(ep.address()),
                        rusb::Direction::Out => ep_out = Some(ep.address()),
                    }
                }
                if let (Some(i), Some(o)) = (ep_in, ep_out) {
                    if endpoints.is_some() {
                        return Err("ambiguous vendor interface".into());
                    }
                    endpoints = Some((alt.interface_number(), i, o));
                }
            }
        }
        let (interface, ep_in, ep_out) = endpoints.ok_or("vendor bulk interface missing")?;
        found = Some(Link {
            handle,
            interface,
            ep_in,
            ep_out,
        });
    }
    let link = found.ok_or("X-CAN serial not found")?;
    link.handle.claim_interface(link.interface)?;
    Ok(link)
}

fn run_command(link: &Link, command: &str, args: &[String]) -> Result<()> {
    match command {
        "info" if args.is_empty() => {
            let out = link.exchange(&request(1, 1, &[])?, 64)?;
            let p = validate(&out, 1, 1)?;
            if p.len() != 14 {
                return Err("invalid info length".into());
            }
            let word = |i| u32::from_le_bytes(p[i..i + 4].try_into().unwrap());
            println!(
                "clock_hz={} idcode={:08x} uptime_ms={} termination_declared={} safe={}",
                word(0),
                word(4),
                word(8),
                p[12],
                p[13]
            );
        }
        "echo" if args.len() == 1 => {
            let input = args[0].as_bytes();
            let out = link.exchange(&request(2, 1, input)?, 64)?;
            if validate(&out, 2, 1)? != input {
                return Err("echo mismatch".into());
            }
            println!("echo verified: {} bytes", input.len());
        }
        "self-test" if args.is_empty() => {
            // Bounded USB-only test: no CAN commands or retries.
            for id in 0u32..106 {
                let len = (id % 53) as usize;
                let data: Vec<u8> = (0..len).map(|i| (i as u8).wrapping_mul(37)).collect();
                let split = if id < 53 { 64 } else { 7 };
                let out = link.exchange(&request(2, id, &data)?, split)?;
                if validate(&out, 2, id)? != data {
                    return Err(format!("echo mismatch id={id}").into());
                }
            }
            let mut bad = request(2, 107, &[])?;
            bad[7] = 53;
            let out = link.exchange(&bad, 64)?;
            if out[6] != 1 || &out[..4] != b"XCAN" || out[8..12] != 107u32.to_le_bytes() {
                return Err("invalid-length rejection failed".into());
            }
            println!(
                "PASS: 106 echoes (0..52 bytes, whole/split writes), malformed length rejected"
            );
        }
        _ => {
            return Err(
                "usage: xcan list | info SERIAL | echo SERIAL TEXT | self-test SERIAL".into(),
            )
        }
    }
    Ok(())
}
fn run() -> Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.as_slice() == ["list"] {
        let mut count = 0;
        for device in rusb::devices()?.iter() {
            let desc = device.device_descriptor()?;
            if desc.vendor_id() == VID && desc.product_id() == PID {
                let serial = device.open()?.read_serial_number_string_ascii(&desc)?;
                println!("{:04x}:{:04x} serial={serial}", VID, PID);
                count += 1;
            }
        }
        if count == 0 {
            println!("No X-CAN found");
        }
        return Ok(());
    }
    if args.len() < 2 {
        return Err("usage: xcan list | info SERIAL | echo SERIAL TEXT | self-test SERIAL".into());
    }
    let link = open(&args[1])?;
    let result = run_command(&link, &args[0], &args[2..]);
    let cleanup = link.close();
    match (result, cleanup) {
        (Err(e), Err(c)) => Err(format!("{e}; cleanup: {c}").into()),
        (Err(e), _) => Err(e),
        (_, Err(e)) => Err(e),
        _ => Ok(()),
    }
}
fn main() {
    if let Err(err) = run() {
        eprintln!("ERROR: {err}");
        std::process::exit(1);
    }
}

#[test]
fn framing_boundaries() {
    assert!(request(2, 1, &[0; 53]).is_err());
    for n in 0..=52 {
        let data = vec![0xa5; n];
        let mut f = request(2, 0x12345678, &data).unwrap();
        assert_eq!(validate(&f, 2, 0x12345678).unwrap(), data);
        f[7] = 255;
        assert!(validate(&f, 2, 0x12345678).is_err());
    }
    let f = request(1, 42, &[]).unwrap();
    assert!(validate(&f, 1, 43).is_err());
}
