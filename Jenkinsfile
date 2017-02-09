matrix = [
    [OS: 'el', DIST: '6', PACK: 'rpm'],
    //[OS: 'el', DIST: '7', PACK: 'rpm'],
    [OS: 'fedora', DIST: '24', PACK: 'rpm'],
    [OS: 'ubuntu', DIST: 'precise', PACK: 'deb'],
    [OS: 'ubuntu', DIST: 'trusty', PACK: 'deb'],
    [OS: 'ubuntu', DIST: 'yakkety', PACK: 'deb'],
    [OS: 'debian', DIST: 'jessie', PACK: 'deb'],
]

stage('Build'){
    packpack = new org.tarantool.packpack()
    node {
        checkout scm
        packpack.prepareSources()
    }
    packpack.packpackBuildMatrix('result', matrix)
}
