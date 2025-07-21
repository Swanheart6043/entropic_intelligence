export const PhotoWall = () => {
  return (
    <div style={{ height: '100%', padding: '20px', display: 'flex', gap: '24px' }}>
      {[1, 2, 3].map(() => (
        <div style={{ 
          height: '182px',
          flex: '1',
          backgroundColor: '#fff',
          borderRadius: '8px',
          boxShadow: '0 1px 2px 0 rgba(0, 0, 0, 0.03), 0 1px 6px -1px rgba(0, 0, 0, 0.02), 0 2px 4px 0 rgba(0, 0, 0, 0.02)', 
        }}>
          <img style={{ width: '100%' }} src="https://images.pexels.com/photos/2286895/pexels-photo-2286895.jpeg?auto=compress&cs=tinysrgb&w=1260&h=750&dpr=2" alt="" />
        </div>
      ))}
    </div>
  )
}