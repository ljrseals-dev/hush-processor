#include "audio.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <atomic>
#include <cerrno>
#include <system_error>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <limits>
#include <stdexcept>
namespace fk::audio {
namespace {
std::uint16_t u16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t u32(const unsigned char* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1])<<8) | (static_cast<std::uint32_t>(p[2])<<16) | (static_cast<std::uint32_t>(p[3])<<24); }
void put16(std::FILE* f, std::uint16_t v) { std::fputc(static_cast<int>(v & 255U), f); std::fputc(static_cast<int>((v >> 8) & 255U), f); }
void put32(std::FILE* f, std::uint32_t v) { for (int i=0;i<4;++i) std::fputc(static_cast<int>((v >> (8*i)) & 255U), f); }
bool write_exact(std::FILE* f, const void* data, std::size_t size) { return std::fwrite(data, 1, size, f) == size; }
bool tag(const unsigned char* p, const char* s) { return std::memcmp(p,s,4)==0; }
}
std::size_t AudioBuffer::frames() const { return channels ? samples.size()/channels : 0; }
void AudioBuffer::validate() const {
 if(sample_rate<8000||sample_rate>768000) throw std::runtime_error("sample rate outside supported range 8000..768000 Hz");
 if(channels==0||channels>32) throw std::runtime_error("channel count outside supported range 1..32");
 if(samples.empty()||samples.size()%channels) throw std::runtime_error("audio buffer is empty or has incomplete frames");
 for(float x:samples) if(!std::isfinite(x)) throw std::runtime_error("audio contains NaN or infinite samples");
}
AudioBuffer read_wav(const std::filesystem::path& path) {
 std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("cannot open input: "+path.string());
 f.seekg(0,std::ios::end); auto end=f.tellg();
 if(end<44||end>static_cast<std::streamoff>(2ULL*1024*1024*1024)) throw std::runtime_error("WAV file size invalid or exceeds 2 GiB limit");
 f.seekg(0); std::vector<unsigned char>b(static_cast<std::size_t>(end)); f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));
 if(!f||!tag(b.data(),"RIFF")||!tag(b.data()+8,"WAVE")) throw std::runtime_error("not a supported RIFF/WAVE file");
 std::uint16_t fmt=0,ch=0,bits=0,block=0; std::uint32_t sr=0; std::size_t data_at=0,data_size=0;
 for(std::size_t p=12;p+8<=b.size();) { auto n=static_cast<std::size_t>(u32(b.data()+p+4)); if(n>b.size()-p-8) throw std::runtime_error("malformed WAV chunk length");
  if(tag(b.data()+p,"fmt ")) { if(n<16) throw std::runtime_error("WAV fmt chunk too short"); fmt=u16(b.data()+p+8);ch=u16(b.data()+p+10);sr=u32(b.data()+p+12);block=u16(b.data()+p+20);bits=u16(b.data()+p+22); if(fmt==0xFFFE&&n>=40)fmt=u16(b.data()+p+32); }
  else if(tag(b.data()+p,"data")){data_at=p+8;data_size=n;}
  p+=8+n+(n&1U);
 }
 if(!data_at||!ch||!sr||!block) throw std::runtime_error("WAV missing required format or data chunk");
 if(!((fmt==1&&(bits==8||bits==16||bits==24||bits==32))||(fmt==3&&bits==32))) throw std::runtime_error("supported encodings: PCM 8/16/24/32-bit and IEEE float32");
 const std::size_t bytes=bits/8; if(block!=ch*bytes||data_size%block) throw std::runtime_error("inconsistent WAV block alignment or data length");
 AudioBuffer out{sr,ch,{}};out.samples.reserve(data_size/bytes);const unsigned char*d=b.data()+data_at;
 for(std::size_t i=0;i<data_size;i+=bytes){float x=0;
  if(fmt==3)std::memcpy(&x,d+i,4); else if(bits==8)x=(static_cast<int>(d[i])-128)/128.0f;
  else if(bits==16)x=static_cast<std::int16_t>(u16(d+i))/32768.0f;
  else if(bits==24){const std::uint32_t raw=static_cast<std::uint32_t>(d[i])|(static_cast<std::uint32_t>(d[i+1])<<8)|(static_cast<std::uint32_t>(d[i+2])<<16);const std::int32_t v=(raw&0x800000U)?static_cast<std::int32_t>(raw)-0x1000000:static_cast<std::int32_t>(raw);x=static_cast<float>(static_cast<double>(v)/8388608.0);}
  else x=static_cast<float>(static_cast<double>(std::bit_cast<std::int32_t>(u32(d+i)))/2147483648.0);
  if(!std::isfinite(x))throw std::runtime_error("WAV contains non-finite sample");
  out.samples.push_back(x);
 }
 out.validate();return out;
}
void write_wav_pcm16(const std::filesystem::path& path, const AudioBuffer& a, bool overwrite) {
 a.validate();
 const std::uint64_t data64=static_cast<std::uint64_t>(a.samples.size())*2;
 if(data64>std::numeric_limits<std::uint32_t>::max()-36) throw std::runtime_error("output WAV exceeds RIFF size limit");
 namespace fs = std::filesystem;
 std::error_code ec;
 const auto dst_status = fs::symlink_status(path, ec);
 if (ec && ec != std::errc::no_such_file_or_directory) throw std::runtime_error("cannot inspect output path: " + path.string());
 if (!ec && fs::is_symlink(dst_status)) throw std::runtime_error("refusing to write through a symbolic link: " + path.string());
 ec.clear();
 const bool exists = fs::exists(path, ec);
 if (ec) throw std::runtime_error("cannot inspect output path: " + path.string());
 if (exists) {
   const auto st = fs::status(path, ec);
   if (ec || !fs::is_regular_file(st)) throw std::runtime_error("WAV output must be a regular file: " + path.string());
   if (!overwrite) throw std::runtime_error("output exists; explicit overwrite permission required: " + path.string());
 }
 const fs::path parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
#if !defined(_WIN32)
 static std::atomic<std::uint64_t> nonce{0};
 const fs::path temp = parent / ("." + path.filename().string() + ".hush-wav-tmp-" + std::to_string(static_cast<unsigned long long>(::getpid())) + "-" + std::to_string(nonce.fetch_add(1)));
 int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
 flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
 flags |= O_CLOEXEC;
#endif
 const int fd = ::open(temp.c_str(), flags, S_IRUSR | S_IWUSR);
 if (fd < 0) throw std::runtime_error("cannot create safe temporary WAV: " + std::string(std::strerror(errno)));
 std::FILE* f = ::fdopen(fd, "wb");
 if (!f) { const int e = errno; ::close(fd); ::unlink(temp.c_str()); throw std::runtime_error("cannot open temporary WAV stream: " + std::string(std::strerror(e))); }
#else
 const fs::path temp = parent / (path.filename().string() + ".hush-wav-tmp");
 if (fs::exists(temp, ec)) throw std::runtime_error("temporary WAV path already exists");
 std::FILE* f = std::fopen(temp.string().c_str(), "wbx");
 if (!f) throw std::runtime_error("cannot create temporary WAV file");
#endif
 bool okay = true;
 okay = okay && write_exact(f, "RIFF", 4); put32(f, static_cast<std::uint32_t>(36+data64));
 okay = okay && write_exact(f, "WAVE", 4) && write_exact(f, "fmt ", 4); put32(f,16); put16(f,1); put16(f,a.channels);
 put32(f,a.sample_rate); put32(f,a.sample_rate*a.channels*2); put16(f,static_cast<std::uint16_t>(a.channels*2)); put16(f,16);
 okay = okay && write_exact(f, "data", 4); put32(f,static_cast<std::uint32_t>(data64));
 for(float sample:a.samples) { double cl=std::clamp(static_cast<double>(sample),-1.0,1.0); auto v=static_cast<std::int16_t>(std::lrint(cl*32767.0)); put16(f,static_cast<std::uint16_t>(v)); }
 if (std::fflush(f) != 0) okay = false;
#if !defined(_WIN32)
 if (okay && ::fsync(::fileno(f)) != 0) okay = false;
#endif
 if (std::fclose(f) != 0) okay = false;
 if (!okay) { const int e = errno;
#if !defined(_WIN32)
   ::unlink(temp.c_str());
#else
   fs::remove(temp, ec);
#endif
   throw std::runtime_error("failed writing temporary WAV: " + std::string(std::strerror(e))); }
#if !defined(_WIN32)
 if (overwrite) {
   if (::rename(temp.c_str(), path.c_str()) != 0) { const int e=errno; ::unlink(temp.c_str()); throw std::runtime_error("atomic WAV replace failed: " + std::string(std::strerror(e))); }
 } else {
   if (::link(temp.c_str(), path.c_str()) != 0) { const int e=errno; ::unlink(temp.c_str()); throw std::runtime_error("atomic no-overwrite WAV create failed: " + std::string(std::strerror(e))); }
   if (::unlink(temp.c_str()) != 0) throw std::runtime_error("WAV was created but temporary link cleanup failed");
 }
 const int dirfd = ::open(parent.c_str(), O_RDONLY
#ifdef O_DIRECTORY
   | O_DIRECTORY
#endif
 );
 if (dirfd >= 0) { (void)::fsync(dirfd); (void)::close(dirfd); }
#else
 if (!overwrite && fs::exists(path, ec)) { fs::remove(temp, ec); throw std::runtime_error("output exists; explicit overwrite permission required: " + path.string()); }
 if (overwrite) fs::remove(path, ec);
 fs::rename(temp, path, ec); if (ec) { fs::remove(temp); throw std::runtime_error("WAV replace failed: " + ec.message()); }
#endif
}
Stats analyze(const AudioBuffer&a){a.validate();Stats s{};long double squares=0,sum=0;for(float x:a.samples){double d=x;s.peak=std::max(s.peak,std::abs(d));squares+=d*d;sum+=d;if(std::abs(d)>=1.0)++s.clipped_samples;}s.rms=std::sqrt(static_cast<double>(squares/a.samples.size()));s.dc=static_cast<double>(sum/a.samples.size());return s;}
AudioBuffer apply_gain(const AudioBuffer&a,double db){a.validate();if(!std::isfinite(db)||db< -120||db>36)throw std::runtime_error("gain must be between -120 and +36 dB");AudioBuffer o=a;float g=static_cast<float>(std::pow(10.0,db/20.0));for(auto&x:o.samples)x*=g;return o;}
AudioBuffer mix(const std::vector<AudioBuffer>&in,const std::vector<double>&gains){if(in.empty()||in.size()!=gains.size())throw std::runtime_error("mix requires matching non-empty input and gain lists");auto sr=in[0].sample_rate;auto ch=in[0].channels;std::size_t n=0;for(std::size_t i=0;i<in.size();++i){in[i].validate();if(in[i].sample_rate!=sr||in[i].channels!=ch)throw std::runtime_error("mix inputs must match sample rate and channels");if(!std::isfinite(gains[i])||gains[i]<-120||gains[i]>36)throw std::runtime_error("mix gain must be between -120 and +36 dB");n=std::max(n,in[i].samples.size());}AudioBuffer o{sr,ch,std::vector<float>(n,0)};for(std::size_t k=0;k<in.size();++k){float g=static_cast<float>(std::pow(10.0,gains[k]/20.0));for(std::size_t j=0;j<in[k].samples.size();++j)o.samples[j]+=in[k].samples[j]*g;}return o;}
AudioBuffer make_tone(double sec,double hz,double amp,std::uint32_t sr,std::uint16_t ch){if(!std::isfinite(sec)||sec<=0||sec>3600)throw std::runtime_error("duration must be >0 and <=3600 seconds");if(!std::isfinite(hz)||hz<=0||hz>=sr/2.0)throw std::runtime_error("frequency must be above 0 and below Nyquist");if(!std::isfinite(amp)||amp<0||amp>1)throw std::runtime_error("amplitude must be in [0,1]");if(sr<8000||sr>768000||ch==0||ch>32)throw std::runtime_error("invalid sample rate or channel count");auto frames=static_cast<std::size_t>(std::llround(sec*sr));AudioBuffer a{sr,ch,std::vector<float>(frames*ch)};constexpr double pi=3.14159265358979323846;for(std::size_t i=0;i<frames;++i){float x=static_cast<float>(amp*std::sin(2*pi*hz*i/sr));for(std::size_t c=0;c<ch;++c)a.samples[i*ch+c]=x;}return a;}
std::string describe(const AudioBuffer&a){auto s=analyze(a);return "sample_rate="+std::to_string(a.sample_rate)+" Hz\nchannels="+std::to_string(a.channels)+"\nframes="+std::to_string(a.frames())+"\nduration_seconds="+std::to_string(double(a.frames())/a.sample_rate)+"\npeak_dbfs="+std::to_string(s.peak>0?20*std::log10(s.peak):-120.0)+"\nrms_dbfs="+std::to_string(s.rms>0?20*std::log10(s.rms):-120.0)+"\ndc_offset="+std::to_string(s.dc)+"\nclipped_samples="+std::to_string(s.clipped_samples);}
}
