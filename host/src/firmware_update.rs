use sha2::{Digest, Sha256};

pub const CHUNK_SIZE: usize = 512;
pub const MAX_IMAGE_SIZE: usize = 0xc000;
pub const IMAGE_MAGIC: [u8; 4] = [0x3d, 0xb8, 0xf3, 0x96];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Status {
    pub state: u8,
    pub error: u8,
    pub image_size: u32,
    pub next_offset: u32,
    pub erase_offset: u32,
}

pub fn begin_payload(image: &[u8]) -> Result<Vec<u8>, &'static str> {
    if image.len() < 520 || image.len() > MAX_IMAGE_SIZE || image[..4] != IMAGE_MAGIC {
        return Err("not a bounded MCUboot signed image");
    }
    let mut payload = Vec::with_capacity(36);
    payload.extend_from_slice(&(image.len() as u32).to_le_bytes());
    payload.extend_from_slice(&Sha256::digest(image));
    Ok(payload)
}

pub fn write_payload(offset: usize, data: &[u8]) -> Result<Vec<u8>, &'static str> {
    if offset > u32::MAX as usize || offset & 7 != 0 || data.is_empty() || data.len() > CHUNK_SIZE {
        return Err("invalid firmware chunk");
    }
    let mut payload = Vec::with_capacity(8 + data.len());
    payload.extend_from_slice(&(offset as u32).to_le_bytes());
    payload.extend_from_slice(&(data.len() as u16).to_le_bytes());
    payload.extend_from_slice(&[0, 0]);
    payload.extend_from_slice(data);
    Ok(payload)
}

pub fn parse_status(payload: &[u8]) -> Result<Status, &'static str> {
    if payload.len() != 16 || payload[0] > 5 || payload[1] > 6 || payload[2..4] != [0, 0] {
        return Err("invalid firmware status");
    }
    let word = |offset| u32::from_le_bytes(payload[offset..offset + 4].try_into().unwrap());
    Ok(Status {
        state: payload[0],
        error: payload[1],
        image_size: word(4),
        next_offset: word(8),
        erase_offset: word(12),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signed_image_is_hashed_and_split_into_bounded_chunks() {
        let mut image = vec![0xa5; 30_823];
        image[..4].copy_from_slice(&IMAGE_MAGIC);
        let begin = begin_payload(&image).unwrap();
        assert_eq!(
            u32::from_le_bytes(begin[..4].try_into().unwrap()),
            image.len() as u32
        );
        assert_eq!(&begin[4..], Sha256::digest(&image).as_slice());
        let chunks: Vec<_> = image
            .chunks(CHUNK_SIZE)
            .enumerate()
            .map(|(i, data)| write_payload(i * CHUNK_SIZE, data).unwrap())
            .collect();
        assert_eq!(chunks.len(), 61);
        assert_eq!(chunks[0].len(), 520);
        assert_eq!(chunks.last().unwrap().len(), 8 + image.len() % CHUNK_SIZE);
        assert!(write_payload(1, &[1]).is_err());
        assert!(write_payload(0, &[0; CHUNK_SIZE + 1]).is_err());
    }

    #[test]
    fn status_is_strictly_decoded() {
        let mut wire = [0u8; 16];
        wire[0] = 3;
        wire[4..8].copy_from_slice(&30_823u32.to_le_bytes());
        wire[8..12].copy_from_slice(&512u32.to_le_bytes());
        wire[12..16].copy_from_slice(&0xd000u32.to_le_bytes());
        let status = parse_status(&wire).unwrap();
        assert_eq!(status.next_offset, 512);
        wire[2] = 1;
        assert!(parse_status(&wire).is_err());
    }
}
